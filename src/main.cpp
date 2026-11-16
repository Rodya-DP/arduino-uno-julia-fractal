#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <EEPROM.h>

MCUFRIEND_kbv tft;

// ================== SETTINGS ==================
#define SCR_W   320
#define SCR_H   240
#define MAX_ITER 12
#define EEPROM_ADDR 0
#define JULIA_LIMIT 1024  // Squared escape radius, in Q8.8 fixed-point units

const uint8_t scaleOptions[3] = { 2, 4, 6 };

#define SHIFT_STEP 13  // Color-cycle speed; 13/256 per frame
#define SHAPE_STEP 1   // Shape animation speed; 1 = smooth interpolation
// ================================================

uint16_t fullPalette[64];  
uint16_t palFrame[MAX_ITER];
uint16_t lineBuffer[SCR_W];

int16_t trajRe[64];
int16_t trajIm[64];

uint8_t  scaleIdx = 0;
uint8_t  scale = 2;

uint16_t shift = 0;        
uint16_t shapePhase = 0;   

int16_t x0_, y0_, dx_, dy_;
uint16_t fracW, fracH;

void updateGrid();
void renderJulia(int16_t cRe, int16_t cIm);
void buildFullPalette();
void buildTrajectory();
void buildPaletteFrame(uint8_t base, uint8_t paletteFraction);
uint16_t blend565(uint16_t a, uint16_t b, uint8_t weight);

// ----------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  uint8_t saved = EEPROM.read(EEPROM_ADDR);
  if (saved >= 3 || saved == 255) saved = 0;
  else saved = (saved + 1) % 3;
  scaleIdx = saved;
  EEPROM.update(EEPROM_ADDR, scaleIdx);
  scale = scaleOptions[scaleIdx];

  Serial.print(F("Scale = ")); Serial.println(scale);

  // --- Display ID diagnostics ---
  uint16_t displayId = tft.readID();
  Serial.print(F("TFT ID: 0x")); Serial.println(displayId, HEX);
  
  // If the display ID cannot be read, assume a common ILI9486-based 2.4" shield.
  if (displayId == 0x0000 || displayId == 0xFFFF) {
    displayId = 0x9486;
    Serial.println(F("ID not detected; using fallback 0x9486"));
  }

  tft.begin(displayId);
  tft.setRotation(1);
  tft.fillScreen(0x0000);

  buildFullPalette();
  buildTrajectory();
  updateGrid();
}

// ----------------------------------------------------------------
void updateGrid() {
  fracW = SCR_W / scale;
  fracH = SCR_H / scale;
  x0_ = -410;
  y0_ = -307;
  dx_ = 820 / fracW;
  dy_ = 614 / fracH;
}

// ----------------------------------------------------------------
void buildFullPalette() {
  for (int i = 0; i < 64; i++) {
    float angle = i * (2.0f * 3.14159265f / 64.0f);
    uint8_t r = 127 + 127 * sin(angle);
    uint8_t g = 127 + 127 * sin(angle + 2.094f);
    uint8_t b = 127 + 127 * sin(angle + 4.188f);
    fullPalette[i] = ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
  }
}

// ----------------------------------------------------------------
void buildTrajectory() {
  for (int i = 0; i < 64; i++) {
    float angle = i * (2.0f * 3.14159265f / 64.0f);
    float cReFloat = -0.72f + 0.20f * cos(angle);
    float cImFloat = 0.16f + 0.22f * sin(angle * 0.85f); 
    trajRe[i] = (int16_t)(cReFloat * 256.0f);
    trajIm[i] = (int16_t)(cImFloat * 256.0f);
  }
}

// ----------------------------------------------------------------
uint16_t blend565(uint16_t a, uint16_t b, uint8_t weight) {
  int16_t ar = (a >> 11) & 0x1F;
  int16_t ag = (a >> 5)  & 0x3F;
  int16_t ab =  a        & 0x1F;
  int16_t br = (b >> 11) & 0x1F;
  int16_t bg = (b >> 5)  & 0x3F;
  int16_t bb =  b        & 0x1F;
  uint8_t r  = ar + ((br - ar) * (int16_t)weight) / 256;
  uint8_t g  = ag + ((bg - ag) * (int16_t)weight) / 256;
  uint8_t bl = ab + ((bb - ab) * (int16_t)weight) / 256;
  return ((uint16_t)r << 11) | ((uint16_t)g << 5) | bl;
}

// ----------------------------------------------------------------
void buildPaletteFrame(uint8_t base, uint8_t paletteFraction) {
  for (uint8_t i = 0; i < MAX_ITER; i++) {
    uint16_t idx = (i * 7 + base) & 0xFF;
    
    uint8_t posA = idx >> 2;
    uint8_t posB = (posA + 1) & 63;
    uint8_t fracA = (idx & 3) * 64;
    
    uint16_t cA = fullPalette[posA];
    uint16_t cB = fullPalette[posB];
    uint16_t colorA = (fracA == 0) ? cA : blend565(cA, cB, fracA);

    if (paletteFraction == 0) {
      palFrame[i] = colorA;
    } else {
      uint16_t nextIdx = (idx + 1) & 0xFF;
      uint8_t posC = nextIdx >> 2;
      uint8_t posD = (posC + 1) & 63;
      uint8_t fracC = (nextIdx & 3) * 64;
      
      uint16_t cC = fullPalette[posC];
      uint16_t cD = fullPalette[posD];
      uint16_t colorB = (fracC == 0) ? cC : blend565(cC, cD, fracC);
      
      palFrame[i] = blend565(colorA, colorB, paletteFraction);
    }
  }
}

// ----------------------------------------------------------------
void loop() {
  shapePhase += SHAPE_STEP;
  
  uint8_t trajectoryIndex = (shapePhase >> 2) & 63; 
  uint8_t interpolation = (shapePhase & 3) * 64; 
  uint8_t nextIndex = (trajectoryIndex + 1) & 63;

  int16_t cRe = trajRe[trajectoryIndex] + (((int32_t)(trajRe[nextIndex] - trajRe[trajectoryIndex]) * interpolation) >> 8);
  int16_t cIm = trajIm[trajectoryIndex] + (((int32_t)(trajIm[nextIndex] - trajIm[trajectoryIndex]) * interpolation) >> 8);

  shift += SHIFT_STEP;
  uint8_t base = shift >> 8;
  uint8_t paletteFraction = shift & 0xFF;

  buildPaletteFrame(base, paletteFraction);
  renderJulia(cRe, cIm);
}

// ----------------------------------------------------------------
// Renders one frame of the Julia set using Q8.8 fixed-point math.
// The result is written to the TFT in horizontal scale-sized blocks.
void renderJulia(int16_t cRe, int16_t cIm) {
  for (uint16_t py = 0; py < fracH; py++) {
    int16_t y = y0_ + (int16_t)(py * dy_);
    int16_t x = x0_;
    uint16_t* buf = lineBuffer;

    for (uint16_t px = 0; px < fracW; px++) {
      int16_t zx = x;
      int16_t zy = y;
      uint8_t iter = 0;

      while (iter < MAX_ITER) {
        int16_t zx2 = (int16_t)(((int32_t)zx * zx) >> 8);
        int16_t zy2 = (int16_t)(((int32_t)zy * zy) >> 8);
        if (zx2 + zy2 > JULIA_LIMIT) break;

        int16_t zxy = (int16_t)(((int32_t)zx * zy) >> 8);
        zx = zx2 - zy2 + cRe;
        zy = (int16_t)((zxy << 1) + cIm);
        iter++;
      }

      uint16_t color = (iter >= MAX_ITER) ? 0x0000 : palFrame[iter];

      if (scale == 2) {
        *buf++ = color; *buf++ = color;
      } else if (scale == 4) {
        *buf++ = color; *buf++ = color;
        *buf++ = color; *buf++ = color;
      } else {
        for (uint8_t s = 0; s < scale; s++) *buf++ = color;
      }
      x += dx_;
    }

    // Pad the remainder of the row with black (needed for scale = 6)
    while (buf < lineBuffer + SCR_W) {
      *buf++ = 0x0000;
    }

    uint16_t yStart = py * scale;
    uint16_t yEnd = yStart + scale - 1;
    if (yEnd >= SCR_H) yEnd = SCR_H - 1;

    tft.setAddrWindow(0, yStart, SCR_W - 1, yEnd);

    for (uint8_t sy = 0; sy <= (yEnd - yStart); sy++) {
      tft.pushColors(lineBuffer, SCR_W, sy == 0);
    }
  }
}