// C stdlib headers
#include <stdio.h>    // For logging
#include <stdlib.h>   // For memory management with malloc() and free()
#include <math.h>     // For trig functions, I'm not writing those myself!

// My base library
#include <OAK/base.h>

// SDL2, provides windowing and pixel drawing
#include <SDL2/SDL.h>

// How much to scale down the resolution by (if this was shown in binary, it
// really should only have a single one, so to speak). For some unknown reason
// the value two really throws this thing off, so avoid it. Also, hopefully
// it is clear that this should be strictly unsigned.
#define SCALE 1

// State of the application - in global variable form (I should fix this)
struct ApplicationState {
  SDL_Window* window;     // The window handle
  SDL_Renderer* renderer; // The handle of SDL2's pixel renderer
  SDL_Event event;        // For checking if the window needs to close
  bool running;           // Whether the should still be active
  u32 ticks;              // The number of frames that have ocurred so far

  f32* z_buffer;          // The depth buffer
  
  // Stuff for the projection
  f32 fov;                // Field of view
  f32 focal_length;       // How far from origin in z direction is viewer

  // Window dimensions
  u32 window_width;
  u32 window_height;

  // Delta time (time between frames)
  f32 deltaTime;
} state;

// For the projection
f32 fov_r;                // The FOV in radians
f32 aspect_ratio;         // The window's height / the window's width
f32 scale;                // another sortof const for projection
f32 x_multiplier;         // what x is multiplied by in project()
f32 y_multiplier;         // what y is multiplied by in project()

typedef struct {
  // The vertex data and amounts
  v3f32* points;
  Color* cols;
  u32 numPoints;

  // The indices per triangle
  v3i32* tris;
  u32 numTris;
} Mesh;

// Puts a pixel to the screen with the specified color and position
void putpixel(i32 x, i32 y, Color col) {
  SDL_SetRenderDrawColor(state.renderer, col.r, col.g, col.b, col.a);
  SDL_RenderDrawPoint(state.renderer, x, y);
}

// Clears the screen
void clearScreen(Color col) {
  SDL_SetRenderDrawColor(state.renderer, col.r, col.g, col.b, col.a);
  SDL_RenderClear(state.renderer);
}

// Swap buffers
void showScreen() {
  SDL_RenderPresent(state.renderer);
}

// Converts from scree space to pixel space, so that a point can be drawn.
v2i32 screenSpace(v2f32 p) {
  v2i32 res = MAKE_V2I32(0, 0);
  // Make origin top left, not middle
  p.x += 1; p.x /= 2;
  p.y += 1; p.y /= 2;
  // Scale to screen size
  res.x = p.x * state.window_width;
  res.y = p.y * state.window_height;
  // Flip y coord - no longer top to bottom
  res.y = state.window_height-res.y;
  // Make sure we don't try rendering pixels off the WINDOW - SDL won't
  // complain, but we won't see them either - this just puts them to the
  // nearest point on the screen, but an assertion could work too. I don't
  // want to do that though, because I have a feeling I would never be able
  // to run my program again. It isn't worth the hours of debugging.
  res.x = CLAMP(res.x, 0, (i32)state.window_width-1);
  res.y = CLAMP(res.y, 0, (i32)state.window_height-1);
  return res;
}

// A function that implements rectilinear perspective projection, using the vars
// FOV, WINDOW_WIDTH, and WINDOW_HEIGHT which have been define above. It returns
// the same Z coord put in, but changes the x and y to allow them to be properly
// displayed on the screen.
v3f32 project(v3f32 p) {
  v3f32 res = MAKE_V3F32(0, 0, 0);
  res.z = -p.z;
  f32 z = p.z == 0 ? 1 : p.z;
  res.x = x_multiplier * p.x / (z - state.focal_length);
  res.y = y_multiplier * p.y / (z - state.focal_length);
  return res;
}

// A function that takes in a vertex and an angle and then returns the vertex
// rotated around the x axis by the angle.
v3f32 rotateX(v3f32 p, f32 theta) {
  v3f32 res = MAKE_V3F32(0, 0, 0);
  f32 c = cos(theta);
  f32 s = sin(theta);
  
  res.x = p.x;
  res.y = c * p.y + p.z * s;
  res.z = c * p.z - p.y * s;

  return res;
}

// A function that takes in a vertex and an angle and then returns the vertex
// rotated around the y axis by the angle.
v3f32 rotateY(v3f32 p, f32 theta) {
  v3f32 res = MAKE_V3F32(0, 0, 0);
  f32 c = cos(theta);
  f32 s = sin(theta);
  
  res.x = c * p.x - p.z * s;
  res.y = p.y;
  res.z = c * p.z + p.x * s;

  return res;
}

// A function that takes in a vertex and an angle and then returns the vertex
// rotated around the z axis by the angle.
v3f32 rotateZ(v3f32 p, f32 theta) {
  v3f32 res = MAKE_V3F32(0, 0, 0);
  f32 c = cos(theta);
  f32 s = sin(theta);
  
  res.x = c * p.x + p.y * s;
  res.y = c * p.y - p.x * s;
  res.z = p.z;

  return res;
}

// Helper from drawTriangle
i32 edgeCross(v2i32 a, v2i32 b, v2i32 p) {
  v2i32 ab = MAKE_V2I32(b.x - a.x, b.y - a.y);
  v2i32 ap = MAKE_V2I32(p.x - a.x, p.y - a.y);
  return v2i32_cross(ab, ap);
}

// Draw a triangle to the screen
void drawTriangle(v2i32 v0, v2i32 v1, v2i32 v2, f32 zCoords[3], Color cols[3]) {
  // Get the bounding box of the triangle
  u32 maxX = MAX(MAX(v0.x, v1.x), v2.x);
  u32 maxY = MAX(MAX(v0.y, v1.y), v2.y);
  u32 minX = MIN(MIN(v0.x, v1.x), v2.x);
  u32 minY = MIN(MIN(v0.y, v1.y), v2.y);

  f32 z0 = zCoords[0];
  f32 z1 = zCoords[1];
  f32 z2 = zCoords[2];

  // Get the winding order - change it if it is not right (i genuinely don't
  // know whether this ensures all are clockwise or anit-clockwise, but it does
  // not change the implementation)
  if (edgeCross(v0, v1, v2) < 0) {
    SWAP(v1, v0);
    SWAP(z1, z0);
  }

  // Compute "area" (for the barycentric coordinate magic)
  f32 area = edgeCross(v0, v1, v2);

  // Iterate over every pixel in bounding box
  for (u32 y = minY; y < maxY; y++) {
    for (u32 x = minX; x < maxX; x++) {
      v2i32 point = MAKE_V2I32(x, y);

      // Wierd math magic to find out if inside triangle (I don't understand it,
      // but then again I don't need to as long as it keeps working)
      i32 w0 = edgeCross(v1, v2, point);
      i32 w1 = edgeCross(v2, v0, point);
      i32 w2 = edgeCross(v0, v1, point);

      bool inTri = w0 >= 0 && w1 >= 0 && w2 >= 0;
      // If it is in the triangle, draw it
      if (inTri) {
        // Barycentric coordinates
        f32 alpha = w0 / (f32)area;
        f32 beta = w1 / (f32)area;
        f32 gamma = w2 / (f32)area;

        // Get the interpolated color
        Color col = WHITE;
        col.r = alpha * cols[0].r + beta * cols[1].r + gamma * cols[2].r;
        col.g = alpha * cols[0].g + beta * cols[1].g + gamma * cols[2].g;
        col.b = alpha * cols[0].b + beta * cols[1].b + gamma * cols[2].b;

        // Get the interpolated z coordinate
        f32 z = alpha * z0 + beta * z1 + gamma * z2;

        // Draw the actual pixel (z buffer)
        if (z < state.z_buffer[x+(y*state.window_width)]) {
          putpixel(point.x, point.y, col);
          state.z_buffer[x+(y*state.window_width)] = z;
        }
      }
    }
  }
}

void renderMesh(Mesh mesh, v3f32 rotation, v3f32 translation) {
  for (u32 i = 0; i < mesh.numTris; i++) {
    v3f32 points[3] = {
        mesh.points[mesh.tris[i].x],
        mesh.points[mesh.tris[i].y],
        mesh.points[mesh.tris[i].z], 
    };

    for (u32 i = 0; i < 3; i++) {
      points[i].x += translation.x;
      points[i].y += translation.y;
      points[i].z += translation.z;
      points[i] = rotateX(points[i], rotation.x);
      points[i] = rotateY(points[i], rotation.y);
      points[i] = rotateZ(points[i], rotation.z);
    }

    v3f32 pointsProj[3] = {
      project(points[0]),
      project(points[1]),
      project(points[2]),
    };
    v2i32 drawPoints[3] = {
      screenSpace(MAKE_V2F32(pointsProj[0].x, pointsProj[0].y)),
      screenSpace(MAKE_V2F32(pointsProj[1].x, pointsProj[1].y)),
      screenSpace(MAKE_V2F32(pointsProj[2].x, pointsProj[2].y)),
    };
    f32 zCoords[3] = {
      pointsProj[0].z,
      pointsProj[1].z,
      pointsProj[2].z,
    };

    v3f32 v0 = points[0];
    v3f32 v1 = points[1];
    v3f32 v2 = points[2];

    v3f32 a = MAKE_V3F32(
        v1.x - v0.x,
        v1.y - v0.y,
        v1.z - v0.y
    );
    v3f32 b = MAKE_V3F32(
        v2.x - v0.x,
        v2.y - v0.y,
        v2.z - v0.y
    );
    
    if (v3f32_cross(a, b).z < 0) {
      SWAP(v0, v1);
    }
    v3f32 V = MAKE_V3F32(
        v1.x - v0.x,
        v1.y - v0.y,
        v1.z - v0.z
    );
    v3f32 W = MAKE_V3F32(
        v2.x - v0.x,
        v2.y - v0.y,
        v2.z - v0.z
    );
    v3f32 normal = v3f32_cross(V, W);
    normal = v3f32_normalize(normal);
    f32 lightIn = v3f32_dot(normal, MAKE_V3F32(0.1, 0.4, 0.5));

    // Sigmoid - scale to range 0-1
    lightIn = 1 / (1+exp(-lightIn));

    //Color cols[3] = {
      //mesh.cols[mesh.tris[i].x],
      //mesh.cols[mesh.tris[i].y],
      //mesh.cols[mesh.tris[i].z],
    //};
    Color cols[3] = { WHITE, WHITE, WHITE };
    for (i32 i = 0; i < 3; i++) {
      cols[i].r *= lightIn;
      cols[i].g *= lightIn;
      cols[i].b *= lightIn;
    }

    drawTriangle(drawPoints[0], drawPoints[1], drawPoints[2], zCoords, cols);
  }
}

// Entry point
i32 main() {
  // Initialize SDL2
  printf("INFO: Initializing SDL2...\n");
  SDL_Init(SDL_INIT_VIDEO);

  // Populate some of state's globals
  state.fov = 60;
  state.window_width = 1280;
  state.window_height = 720;
  state.focal_length = 10;

  // The creation of the window and the renderer to go with it
  printf("INFO: Creating window and renderer...\n");
  state.window = SDL_CreateWindow(
      "SDL2 window", 0, 0, state.window_width, state.window_height, 0
  );
  state.renderer = SDL_CreateRenderer(
      state.window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED
  );

  // Use lower resolution
  state.window_width /= SCALE;
  state.window_height /= SCALE;
  // Make the renderer use a lower resolution, as determined by the SCALE macro
  SDL_RenderSetLogicalSize(
      state.renderer, state.window_width, state.window_height);
  // Make ther renderer support alpha values for colors that aren't 255
  SDL_SetRenderDrawBlendMode(state.renderer, SDL_BLENDMODE_BLEND);

  // Initialize the depth buffer
  state.z_buffer =
    malloc(sizeof(f32) * state.window_width * state.window_height);

  // Populate data for projection matrix
  fov_r = state.fov * pi32 / 180;
  aspect_ratio = (f32)state.window_height / (f32)state.window_width;
  scale = 1 / tan(0.5 * fov_r);
  x_multiplier = aspect_ratio * scale;
  y_multiplier = scale;

  v3f32 cubePoints[8] = {
    MAKE_V3F32(-1, -1, -1),
    MAKE_V3F32(-1, -1, 1),
    MAKE_V3F32(-1, 1, -1),
    MAKE_V3F32(-1, 1, 1),
    MAKE_V3F32(1, -1, -1),
    MAKE_V3F32(1, -1, 1),
    MAKE_V3F32(1, 1, -1),
    MAKE_V3F32(1, 1, 1),
  };
  Color cubeCols[8] = {
    RED, GREEN, BLUE,
    MAGENTA, YELLOW, CYAN,
    RED, GREEN
  };
  v3i32 cubeTris[12] = {
    MAKE_V3I32(1, 0, 2),
    MAKE_V3I32(2, 3, 1),

    MAKE_V3I32(0, 4, 6),
    MAKE_V3I32(6, 2, 0),

    MAKE_V3I32(4, 5, 7),
    MAKE_V3I32(7, 6, 4),

    MAKE_V3I32(5, 1, 3),
    MAKE_V3I32(3, 7, 5),

    MAKE_V3I32(2, 6, 7),
    MAKE_V3I32(7, 3, 2),

    MAKE_V3I32(1, 5, 4),
    MAKE_V3I32(4, 0, 1),
  };
  Mesh cube = {
    .points = cubePoints,
    .cols = cubeCols,
    .numPoints = 8,
    .tris = cubeTris,
    .numTris = 12,
  };

  v3f32 tetPoints[6] = {
    MAKE_V3F32(-0.75, 0, -0.75),
    MAKE_V3F32(-0.75, 0, 0.75),
    MAKE_V3F32(0.75, 0, -0.75),
    MAKE_V3F32(0.75, 0, 0.75),
    MAKE_V3F32(0, 1, 0),
    MAKE_V3F32(0, -1, 0),
  };
  Color tetCols[6] = {
    RED, GREEN, BLUE,
    MAGENTA, CYAN, YELLOW
  };
  v3i32 tetTris[8] = {
    MAKE_V3I32(0, 4, 1),
    MAKE_V3I32(1, 4, 3),
    MAKE_V3I32(3, 4, 2),
    MAKE_V3I32(2, 4, 0),
    MAKE_V3I32(5, 0, 1),
    MAKE_V3I32(5, 1, 3),
    MAKE_V3I32(5, 3, 2),
    MAKE_V3I32(5, 2, 0),
  };
  Mesh tet = {
    .points = tetPoints,
    .cols = tetCols,
    .numPoints = 6,
    .tris = tetTris,
    .numTris = 8,
  };

  // For spinning cube...
  f32 theta = 0;

  // Ticks keeps track of the number of frames that have passed so far, running
  // keeps track of whether the application should still have not closed,
  // deltaTime is the anount of time it takes for one frame to run.
  state.ticks = 0;
  state.running = true;
  state.deltaTime = 1;

  // Main loop
  while (state.running) {
    // FPS Calc - part 1
    u64 start = SDL_GetPerformanceCounter();

    // Event loop (just checks if window needs to close)
    while (SDL_PollEvent(&state.event)) {
      switch (state.event.type) { 
        case SDL_QUIT: {
          printf("INFO: Exiting through SDL_QUIT event...\n");
          state.running = false;
        } break;
        case SDL_WINDOWEVENT: {
          switch (state.event.window.event) {
            case SDL_WINDOWEVENT_RESIZED: {
              state.window_width = state.event.window.data1 / SCALE;
              state.window_height = state.event.window.data2 / SCALE;
              SDL_RenderSetLogicalSize(
                  state.renderer, state.window_width, state.window_height);

              // Resize the depth buffer
              state.z_buffer = realloc(
                  state.z_buffer,
                  sizeof(f32) * state.window_width * state.window_height
              );

              // Populate data for projection matrix
              // Same as before: fov_r = state.fov * pi32 / 180; */
              aspect_ratio = (f32)state.window_height / (f32)state.window_width;
              scale = 1 / tan(0.5 * fov_r);
              x_multiplier = aspect_ratio * scale;
              y_multiplier = scale;
            } break;
            default: break;
          }
        } break;
        case SDL_KEYDOWN: {
          switch (state.event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: {
              state.running = false;
            } break;
            case SDL_SCANCODE_SPACE: {
              for (u32 i = 0; i < 8; i++) {
                srand(SDL_GetTicks() * i * 9876789876877908799llu);
                cube.cols[i] =
                  MAKE_COLOR( rand() % 255, rand() % 255, rand() % 255, 255);
              }
            } break;
            default: break;
          }
        } break;
        default: break;
      }
    }
    // Clear the screen
    clearScreen(BLACK);
    // Clear the depth buffer
    for (u32 i = 0; i < state.window_width * state.window_height; i++)
      state.z_buffer[i] = inf_f32();

    // Update theta
    if (state.ticks > 0) theta += 0.02f / state.deltaTime;

    // Draw scene
    renderMesh(
        cube, MAKE_V3F32(theta, theta * 2, theta * 3), MAKE_V3F32(0, 0, 0));
    renderMesh(
        tet, MAKE_V3F32(0, theta * 3, 0), MAKE_V3F32(0, 0, 3));

    // Swap buffers
    showScreen();

    // Update the title
    if (state.ticks % 100 == 0) {
      char title[48] = "";
      f32 fps = state.deltaTime > 0 ? 1000.0f / state.deltaTime : 1.0f;
      sprintf(title, "FPS: %f\tDelta Time: %f", fps, state.deltaTime);
      SDL_SetWindowTitle(state.window, title);
    }

    // Increment ticks
    state.ticks++;

    // Delta time and FPS
    u64 end = SDL_GetPerformanceCounter();
    state.deltaTime =
      (end - start) / (f32)SDL_GetPerformanceFrequency() * 1000.0f;
  }

  // Free depth buffer
  free(state.z_buffer);

  printf("INFO: Destroying window and renderer...\n");
  SDL_DestroyRenderer(state.renderer);
  SDL_DestroyWindow(state.window);
  printf("INFO: Quitting SDL2...\n");
  SDL_Quit();
  return 0;
}
