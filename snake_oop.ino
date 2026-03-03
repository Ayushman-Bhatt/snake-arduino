#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <stdlib.h>
#include <limits.h>

// ─────────────────────────────────────────
//  CONSTANTS
// ─────────────────────────────────────────
#define GRAPHIC_WIDTH     20
#define GRAPHIC_HEIGHT    8
#define BUTTON_LEFT       5
#define BUTTON_RIGHT      7
#define BUZZER_PIN        9
#define DEBOUNCE_DURATION 10

// ─────────────────────────────────────────
//  NOTE DEFINITIONS (for startup melody)
// ─────────────────────────────────────────
#define NOTE_B4  494
#define NOTE_B5  988
#define NOTE_FS5 740
#define NOTE_DS5 622
#define NOTE_C5  523
#define NOTE_C6  1047
#define NOTE_G6  1568
#define NOTE_E6  1319
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880

// ─────────────────────────────────────────
//  CLASS: Snake
//  Responsibility: owns body positions,
//  handles movement, turning, collision
// ─────────────────────────────────────────
class Snake {
  private:
    struct Pos {
      uint8_t x;
      uint8_t y;
    };

    Pos body[GRAPHIC_HEIGHT * GRAPHIC_WIDTH];
    size_t length;

    enum Direction { LEFT, UP, RIGHT, DOWN } direction;

  public:
    // All possible outcomes of one move
    enum MoveResult { OK, HIT_WALL, HIT_SELF, ATE_APPLE, WON };

    // Constructor
    Snake() {
      reset();
    }

    void reset() {
      body[0].x = 3; body[0].y = 1;
      body[1].x = 2; body[1].y = 1;
      body[2].x = 1; body[2].y = 1;
      body[3].x = 0; body[3].y = 1;
      length = 4;
      direction = RIGHT;
    }

    // Getters — const means these won't modify any data
    size_t  getLength()    const { return length; }
    uint8_t getX(size_t i) const { return body[i].x; }
    uint8_t getY(size_t i) const { return body[i].y; }

    // Turn left = counter-clockwise
    void turnLeft() {
      switch (direction) {
        case LEFT:  direction = DOWN;  break;
        case UP:    direction = LEFT;  break;
        case RIGHT: direction = UP;    break;
        case DOWN:  direction = RIGHT; break;
      }
    }

    // Turn right = clockwise
    void turnRight() {
      switch (direction) {
        case LEFT:  direction = UP;    break;
        case UP:    direction = RIGHT; break;
        case RIGHT: direction = DOWN;  break;
        case DOWN:  direction = LEFT;  break;
      }
    }

    MoveResult move(uint8_t appleX, uint8_t appleY) {
      // STEP 1: Each segment steps into position of segment ahead of it
      // Loop tail→head so we don't overwrite before copying
      for (size_t i = length; i >= 1; i--) {
        body[i] = body[i - 1];
      }

      // STEP 2: Move head in current direction
      switch (direction) {
        case LEFT:  body[0].x--; break;
        case UP:    body[0].y--; break;
        case RIGHT: body[0].x++; break;
        case DOWN:  body[0].y++; break;
      }

      // STEP 3: Wall collision
      // uint8_t is unsigned — if x was 0 and goes left, wraps to 255
      // 255 >= GRAPHIC_WIDTH catches it automatically
      if (body[0].x >= GRAPHIC_WIDTH || body[0].y >= GRAPHIC_HEIGHT) {
        return HIT_WALL;
      }

      // STEP 4: Self collision
      for (size_t i = 1; i < length; i++) {
        if (body[0].x == body[i].x && body[0].y == body[i].y) {
          return HIT_SELF;
        }
      }

      // STEP 5: Apple collision
      if (body[0].x == appleX && body[0].y == appleY) {
        length++; // new tail slot was already prepared by the shift
        if (length >= GRAPHIC_WIDTH * GRAPHIC_HEIGHT) return WON;
        return ATE_APPLE;
      }

      return OK;
    }
};


// ─────────────────────────────────────────
//  CLASS: Apple
//  Responsibility: stores position,
//  respawns avoiding snake body
// ─────────────────────────────────────────
class Apple {
  private:
    uint8_t x, y;

  public:
    Apple() : x(0), y(0) {}

    uint8_t getX() const { return x; }
    uint8_t getY() const { return y; }

    // const Snake& = pass by reference (no copy), promise not to modify
    void respawn(const Snake& snake) {
      bool validPosition;
      do {
        // must pick candidate FIRST then validate — hence do-while
        x = rand() % GRAPHIC_WIDTH;
        y = rand() % GRAPHIC_HEIGHT;
        validPosition = true;

        for (size_t i = 0; i < snake.getLength(); i++) {
          if (x == snake.getX(i) && y == snake.getY(i)) {
            validPosition = false;
            break;
          }
        }
      } while (!validPosition);
    }
};


// ─────────────────────────────────────────
//  CLASS: Display
//  Responsibility: manages LCD, custom
//  characters, virtual buffer flushing
// ─────────────────────────────────────────
class Display {
  private:
    LiquidCrystal_I2C lcd;

    // Virtual screen buffer — write here then flush to LCD at once
    uint8_t graphicRam[GRAPHIC_WIDTH / 4][GRAPHIC_HEIGHT];

    byte blockPixels[3] = { B01110, B01110, B01110 };
    byte applePixels[3] = { B00100, B01010, B00100 };

  public:
    Display() : lcd(0x27, 20, 4) {}

    void init() {
      lcd.init();
      lcd.backlight();
      generateCustomCharacters();
    }

    void clearBuffer() {
      memset(graphicRam, 0, sizeof(graphicRam));
    }

    // itemType: 1 = snake block, 2 = apple
    void addToBuffer(uint8_t x, uint8_t y, uint8_t itemType) {
      graphicRam[x / 4][y] |= itemType << ((x % 4) * 2);
    }

    void flush() {
      lcd.clear();
      for (size_t x = 0; x < 20; x++) {
        for (size_t y = 0; y < 4; y++) {
          // Each LCD row = 2 grid rows (top and bottom half of character)
          uint8_t upperItem = (graphicRam[x / 4][y * 2]     >> ((x % 4) * 2)) & 0x3;
          uint8_t lowerItem = (graphicRam[x / 4][y * 2 + 1] >> ((x % 4) * 2)) & 0x3;

          lcd.setCursor(x, y);
          if (upperItem == 0 && lowerItem == 0) {
            lcd.write(' ');
          } else {
            lcd.write(byte(upperItem * 3 + lowerItem - 1));
          }
        }
      }
    }

    void showMenu() {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(" Snake Game");
      lcd.setCursor(0, 2); lcd.print("Press right button");
      lcd.setCursor(11, 3); lcd.print("to start");
    }

    void showLose(size_t length) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(" You Lose!");
      lcd.setCursor(0, 1); lcd.print("Length: ");
      lcd.setCursor(8, 1); lcd.print(length);
    }

    void showWin(size_t length) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("You Won! Congrats!");
      lcd.setCursor(0, 1); lcd.print("Length: ");
      lcd.setCursor(8, 1); lcd.print(length);
    }

  private:
    void generateCustomCharacters() {
      for (size_t i = 0; i < 8; i++) {
        byte glyph[8];
        memset(glyph, 0, sizeof(glyph));

        int upperIcon = (i + 1) / 3;
        int lowerIcon = (i + 1) % 3;

        if (upperIcon == 1) memcpy(&glyph[0], blockPixels, 3);
        else if (upperIcon == 2) memcpy(&glyph[0], applePixels, 3);

        if (lowerIcon == 1) memcpy(&glyph[4], blockPixels, 3);
        else if (lowerIcon == 2) memcpy(&glyph[4], applePixels, 3);

        lcd.createChar(i, glyph);
      }
      delay(1);
    }
};


// ─────────────────────────────────────────
//  CLASS: Game
//  Responsibility: owns Snake, Apple,
//  Display via COMPOSITION and orchestrates
//  the entire game loop
// ─────────────────────────────────────────
class Game {
  private:
    // COMPOSITION — Game HAS-A Snake, Apple, Display
    Snake   snake;
    Apple   apple;
    Display display;

    enum State { MENU, PLAY, LOSE, WIN } state;

    unsigned long lastUpdateTick;
    unsigned long updateInterval;
    bool          inputHandledThisFrame;

    unsigned long debounceLeft;
    unsigned long debounceRight;

  public:
    Game() : state(MENU),
             lastUpdateTick(0),
             updateInterval(500),
             inputHandledThisFrame(false),
             debounceLeft(0),
             debounceRight(0) {}

    void init() {
      pinMode(BUTTON_LEFT,  INPUT);
      pinMode(BUTTON_RIGHT, INPUT);
      pinMode(BUZZER_PIN,   OUTPUT);
      srand(micros());

      display.init();
      playStartupMelody();
      display.showMenu();
      state = MENU;
    }

    void update() {
      handleInput();

      if (millis() - lastUpdateTick > updateInterval) {
        tick();
        lastUpdateTick = millis();
        inputHandledThisFrame = false;
      }
    }

  private:
    void startGame() {
      snake.reset();
      apple.respawn(snake);
      updateInterval = 500;
      inputHandledThisFrame = false;
      state = PLAY;
    }

    void tick() {
      if (state != PLAY) return;

      // Snake::MoveResult — scope resolution operator :: to access
      // enum defined INSIDE the Snake class
      Snake::MoveResult result = snake.move(apple.getX(), apple.getY());

      switch (result) {
        case Snake::OK:
          break;

        case Snake::ATE_APPLE:
          tone(BUZZER_PIN, 1000); delay(100); noTone(BUZZER_PIN);
          apple.respawn(snake);
          updateInterval = updateInterval * 9 / 10; // speed up 10%
          break;

        case Snake::WON:
          state = WIN;
          break;

        case Snake::HIT_WALL:
        case Snake::HIT_SELF:
          playLoseMelody();
          state = LOSE;
          break;
      }

      render();
    }

    void render() {
      switch (state) {
        case PLAY:
          display.clearBuffer();
          for (size_t i = 0; i < snake.getLength(); i++) {
            display.addToBuffer(snake.getX(i), snake.getY(i), 1);
          }
          display.addToBuffer(apple.getX(), apple.getY(), 2);
          display.flush();
          break;

        case LOSE:
          display.showLose(snake.getLength());
          break;

        case WIN:
          display.showWin(snake.getLength());
          break;

        case MENU:
          break;
      }
    }

    void handleInput() {
      if (digitalRead(BUTTON_LEFT) == HIGH) {
        if (debounceEdge(&debounceLeft) && !inputHandledThisFrame) {
          switch (state) {
            case PLAY: snake.turnLeft(); inputHandledThisFrame = true; break;
            case MENU:
            case LOSE:
            case WIN:  startGame(); break;
          }
        }
      } else {
        debounceLeft = 0;
      }

      if (digitalRead(BUTTON_RIGHT) == HIGH) {
        if (debounceEdge(&debounceRight) && !inputHandledThisFrame) {
          switch (state) {
            case PLAY: snake.turnRight(); inputHandledThisFrame = true; break;
            case MENU:
            case LOSE:
            case WIN:  startGame(); break;
          }
        }
      } else {
        debounceRight = 0;
      }
    }

    // Returns true ONCE on rising edge of button press
    bool debounceEdge(unsigned long* timer) {
      if (*timer == ULONG_MAX) {
        return false;
      } else if (*timer == 0) {
        *timer = millis();
      } else if (millis() - *timer > DEBOUNCE_DURATION) {
        *timer = ULONG_MAX;
        return true;
      }
      return false;
    }

    void playLoseMelody() {
      tone(BUZZER_PIN, 440, 200); delay(200);
      tone(BUZZER_PIN, 415, 200); delay(200);
      tone(BUZZER_PIN, 392, 200); delay(200);
      tone(BUZZER_PIN, 370, 400); delay(400);
      noTone(BUZZER_PIN);
    }

    void playStartupMelody() {
      int melody[] = {
        NOTE_B4, NOTE_B5, NOTE_FS5, NOTE_DS5,
        NOTE_B5, NOTE_FS5, NOTE_DS5, NOTE_C5,
        NOTE_C6, NOTE_G6, NOTE_E6, NOTE_C6, NOTE_G6, NOTE_E6,
        NOTE_B4, NOTE_B5, NOTE_FS5, NOTE_DS5, NOTE_B5,
        NOTE_FS5, NOTE_DS5, NOTE_DS5, NOTE_E5, NOTE_F5,
        NOTE_F5, NOTE_FS5, NOTE_G5, NOTE_G5, NOTE_GS5, NOTE_A5, NOTE_B5
      };
      int durations[] = {
        16, 16, 16, 16, 32, 16, 8, 16,
        16, 16, 16, 32, 16, 8,
        16, 16, 16, 16, 32, 16, 8, 32, 32, 32,
        32, 32, 32, 32, 32, 16, 8
      };
      int size = sizeof(durations) / sizeof(int);
      for (int note = 0; note < size; note++) {
        int duration = 1000 / durations[note];
        tone(BUZZER_PIN, melody[note], duration);
        delay(duration * 1.30);
        noTone(BUZZER_PIN);
      }
    }
};


// ─────────────────────────────────────────
//  MAIN — only 2 lines because OOP handled
//  all the complexity inside the classes
// ─────────────────────────────────────────
Game game;

void setup() {
  game.init();
}

void loop() {
  game.update();
}
