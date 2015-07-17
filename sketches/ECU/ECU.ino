/* --- Libraries --- */
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

/* --- Time Constants --- */
const int WAIT_INTERVAL = 1000;
const int BEGIN_INTERVAL = 2000;
const int TURN45_INTERVAL = 1000;
const int TURN90_INTERVAL = 3000;
const int GRAB_INTERVAL = 1000;
const int STEP_INTERVAL = 2200; // interval to move fully into finishing square

/* --- Serial / Commands --- */
const int BAUD = 9600;
const int OUTPUT_LENGTH = 256;
const int BEGIN_COMMAND = 'B';
const int ALIGN_COMMAND  = 'A';
const int SEEK_COMMAND  = 'S';
const int GRAB_COMMAND  = 'G';
const int TURN_COMMAND  = 'T';
const int JUMP_COMMAND  = 'J';
const int FINISH_COMMAND  = 'F';
const int WAIT_COMMAND = 'W';
const int REPEAT_COMMAND = 'R';
const int UNKNOWN_COMMAND = '?';

/* --- Constants --- */
const int LINE_THRESHOLD = 500; // i.e. 2.5 volts
const int DISTANCE_THRESHOLD = 40; // cm

/* --- I/O Pins --- */
const int CENTER_LINE_PIN = A0;
const int LEFT_LINE_PIN = A1;
const int RIGHT_LINE_PIN = A2;
const int DIST_SENSOR_PIN = 7;
// A4 - A5 reserved

/* --- PWM Servos --- */
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(); // called this way, it uses the default address 0x40
const int FRONT_LEFT_SERVO = 0;
const int FRONT_RIGHT_SERVO = 1;
const int BACK_LEFT_SERVO = 2;
const int BACK_RIGHT_SERVO = 3;
const int ARM_SERVO = 4;
const int MICROSERVO_MIN = 150;
const int MICROSERVO_ZERO = 400; // this is the servo off pulse length
const int MICROSERVO_MAX =  600; // this is the 'maximum' pulse length count (out of 4096)
const int SERVO_MIN = 300;
const int SERVO_OFF = 381; // this is the servo off pulse length
const int SERVO_MAX =  460; // this is the 'maximum' pulse length count (out of 4096)
const int PWM_FREQ = 60; // analog servos run at 60 Hz
const int SERVO_SLOW = 20;
const int SERVO_SPEED = 15;
const int FR = -3;
const int FL = -2;
const int BR = -4;
const int BL = -2;

/* --- Variables --- */
char command;
int result;
int offset = 0;
int at_plant = 0; // 0: not at plant, 1-5: plant number
int at_end = 0; // 0: not at end, 1: 1st end of row, 2: 2nd end of row
int pass_num = 0; // 0: not specified, 1: right-to-left, -1: left-to-rightd

/* --- Buffers --- */
char output[OUTPUT_LENGTH];

/* --- Setup --- */
void setup() {
  Serial.begin(BAUD);
  pinMode(CENTER_LINE_PIN, INPUT);
  pinMode(RIGHT_LINE_PIN, INPUT);
  pinMode(LEFT_LINE_PIN, INPUT);
  pinMode(DIST_SENSOR_PIN, INPUT);
  pwm.begin();
  pwm.setPWMFreq(PWM_FREQ);  // This is the ideal PWM frequency for servos
  pwm.setPWM(FRONT_LEFT_SERVO, 0, SERVO_OFF + FL);
  pwm.setPWM(FRONT_RIGHT_SERVO, 0, SERVO_OFF + FR);
  pwm.setPWM(BACK_LEFT_SERVO, 0, SERVO_OFF + BL);
  pwm.setPWM(BACK_RIGHT_SERVO, 0, SERVO_OFF + BR);
  pwm.setPWM(ARM_SERVO, 0, MICROSERVO_MAX); // it's fixed rotation, not continous
}

/* --- Loop --- */
void loop() {
  if (Serial.available() > 0) {
    char val = Serial.read();
    switch (val) {
    case BEGIN_COMMAND:
      command = BEGIN_COMMAND;
      result = begin_run();
      if (result == 0) { 
        pass_num = 1; 
      }
      break;
    case ALIGN_COMMAND:
      command = ALIGN_COMMAND;
      result = align();
      if (pass_num == 1) {
        at_end = 1;
      }
      else if (pass_num == -1) {
        at_end = 2;
      }
      break;
    case SEEK_COMMAND:
      command = SEEK_COMMAND;
      result = seek_plant();
      if (result == 1) {
        at_plant = 1;
      }
      else {
        at_plant = 0;
        if (pass_num == 1) {
          at_end = 2;
        }
        else if (pass_num == -1) {
          at_end = 1;
        }
      }
      break;
    case GRAB_COMMAND:
      command = GRAB_COMMAND;
      result = grab();
      break;
    case TURN_COMMAND:
      command = TURN_COMMAND;
      result = turn();
      if (result == 0) {
        at_end = 0;
        if (pass_num == 1) {
          pass_num = -1;
        }
      }
      break;
    case JUMP_COMMAND:
      command = JUMP_COMMAND;
      result = jump();
      if (result == 0) {
        at_end = 0;
        pass_num = 1;
      }
      break;
    case FINISH_COMMAND:
      command = FINISH_COMMAND;
      result = finish_run();
      break;
    case REPEAT_COMMAND:
      break;
    case WAIT_COMMAND:
      command = WAIT_COMMAND;
      result = wait();
      break;
    default:
      result = 255;
      command = UNKNOWN_COMMAND;
      break;
    }
    offset = find_offset(LINE_THRESHOLD);
    sprintf(output, "{'command':'%c','result':%d,'at_plant':%d,'at_end':%d,'pass_num':%d,'line':%d}", command, result, at_plant, at_end, pass_num, offset);
    Serial.println(output);
    Serial.flush();
  }
}

/* --- Actions --- */
int begin_run(void) {

  // Move Arm
  pwm.setPWM(ARM_SERVO, 0, MICROSERVO_MIN);

  // Get past black square
  set_servos(5, -30, 5, -30); // Wide left sweep
  delay(3000);

  // Run until line reached
  while (abs(find_offset(LINE_THRESHOLD)) > 2) {
    delay(20);
  }

  // Find Row
  int i = 0;
  int dir = 1;
  while (find_offset(LINE_THRESHOLD) != 0) {
    if ((find_offset(LINE_THRESHOLD) == -255) && (dir = 1)) {
      set_servos(10, -20, 10, -20); // L-curve search
    }
    if ((find_offset(LINE_THRESHOLD) == -255) && (dir = -1)) {
      set_servos(20, -10, 20, -10); // R-curve search
    }
    if (i > 20) {
      i = 0;
      if (dir == 1) {
        dir = -1;
      }
      else if (dir == -1) {
        dir = 1;
      }
    }
    i++;
  }

  // Halt
  set_servos(0, 0, 0, 0);

  return 0;
}

int align(void) {
  /*
    Aligns robot at end of T.
   
   1. Wiggle onto line
   2. Reverse to end of line  
   */

  // Wiggle onto line
  int x = find_offset(LINE_THRESHOLD);
  int i = 0;
  while (i <= 15) {
    x = find_offset(LINE_THRESHOLD);
    Serial.println(x);
    if (x == 0) {
      set_servos(10, -10, 10, -10);
      i++;
    }
    else if (x == -1) {
      set_servos(30, 10, 30, 10);
      i++;
    }
    else if (x == -2) {
      set_servos(-40, -40, -40, -40);
      i = 0;
    }
    else if (x == 1) {
      set_servos(10, -30, 10, -30);
      i++;
    }
    else if (x == 2) {
      set_servos(-40, -40, -40, -40);
      i = 0;
    }
    else if (x == -255) {
      break;
    }
    else if (x == 255) {
      set_servos(15, -15, 15, -15);
      i = 0;
    }
    delay(50);
  }
  set_servos(0, 0, 0, 0); // Halt 

  // Reverse to end
  while (x != 255)  {
    x = find_offset(LINE_THRESHOLD);
    if (x == -1) {
      set_servos(-10, 20, -10, 20);
    }
    else if (x == -2) {
      set_servos(-15, -15, -15, -15);
    }
    else if (x == 1) {
      set_servos(-20, 10, -20, 10);
    }
    else if (x == 2) {
      set_servos(15, 15, 15, 15);
    }
    else if (x == 0) {
      set_servos(-10, 10, -10, 10);
    }
    else if (x == -255) {
      set_servos(0, 0, 0, 0); // Halt 
      return 1;
    }
  }
  set_servos(0, 0, 0, 0); // Halt 
  return 0;
}

int seek_plant(void) {
  at_end = 0; // reset at_end global to zero (no longer will be at end once seek is executed)
  int x = find_offset(LINE_THRESHOLD);
  pwm.setPWM(ARM_SERVO, 0, MICROSERVO_MIN); // Retract arm fully
  delay(GRAB_INTERVAL);
  set_servos(20, -20, 20, -20);
  delay(500);
  
  // Move past plant
  int i = 0;
  while (i < 50)  {
    x = find_offset(LINE_THRESHOLD);
    if (x == -1) {
      set_servos(20, -10, 20, -10);
    }
    else if (x == -2) {
      set_servos(20, 20, 20, 20);
    }
    else if (x == 1) {
      set_servos(10, -20, 10, -20);
    }
    else if (x == 2) {
      set_servos(-20, -20, -20, -20);
    }
    else if (x == 0) {
      set_servos(15, -15, 15, -15);
    }
    delay(50);
    i++;
  }
  
  // Search until next plant
  while (x != 255)  {
    x = find_offset(LINE_THRESHOLD);
    if (x == -1) {
      set_servos(20, -10, 20, -10);
    }
    else if (x == -2) {
      set_servos(20, 20, 20, 20);
    }
    else if (x == 1) {
      set_servos(10, -20, 10, -20);
    }
    else if (x == 2) {
      set_servos(-20, -20, -20, -20);
    }
    else if (x == 0) {
      set_servos(15, -15, 15, -15);
    }
    if (find_distance() < DISTANCE_THRESHOLD) {
      int d = find_distance();
      while (find_distance() <= d) {
        delay(50);
        d = find_distance();
      }
      set_servos(0,0,0,0);
      return 1;
    }
  }
  set_servos(0, 0, 0, 0); // Stop servos
  return 0;
}

int jump(void) {
  // Get past black square
  set_servos(15, -40, 15, -40); // Wide left sweep
  delay(3000);
  // Run until line reached
  while (abs(find_offset(LINE_THRESHOLD)) > 2) {
    delay(20);
  }
  set_servos(0,0,0,0);
  return 0;
}

int turn(void) {
  set_servos(40, 40, 40, 40);
  delay(TURN45_INTERVAL);
  while (abs(find_offset(LINE_THRESHOLD)) > 0) {
    set_servos(40, 40, 40, 40);
  }
  set_servos(0, 0, 0, 0);
  return 0;
}

int grab(void) {
  for (int i = MICROSERVO_MIN; i < MICROSERVO_MAX; i++) {
    pwm.setPWM(ARM_SERVO, 0, i);   // Grab block
    delay(2);
  }
  pwm.setPWM(ARM_SERVO, 0, MICROSERVO_MIN);
  delay(GRAB_INTERVAL);
  return 0;
}

int finish_run(void) {
  set_servos(20, -20, 20, -20); // move forward one space
  delay(STEP_INTERVAL);
  set_servos(25, 25, 25, 25); // turn right 90 degrees
  delay(TURN90_INTERVAL); 
  set_servos(60, -60, 60, -60); // move Forward
  while (find_offset(LINE_THRESHOLD) == -255) {
    delay(100); // Go until finish square
  }
  set_servos(20, -20, 20, -20); // move forward one space
  delay(STEP_INTERVAL);
  set_servos(0, 0, 0, 0); // move forward one space
  return 0;
}

int wait(void) {
  delay(WAIT_INTERVAL);
  return 0;
}

int find_offset(int threshold) {
  int l = analogRead(LEFT_LINE_PIN);
  int c = analogRead(CENTER_LINE_PIN);
  int r = analogRead(RIGHT_LINE_PIN);
  int x;
  if ((l > threshold) && (c < threshold) && (r < threshold)) {
    x = 2; // very off
  }
  else if ((l > threshold) && (c > threshold) && (r < threshold)) {
    x = 1; // midly off
  }
  else if ((l < threshold) && (c > threshold) && (r < threshold)) {
    x = 0; // on target
  }
  else if ((l < threshold) && (c > threshold) && (r > threshold)) {
    x = -1;  // mildy off
  }
  else if ((l < threshold) && (c < threshold) && (r > threshold)) {
    x = -2; // very off
  }
  else if ((l > threshold) && (c > threshold) && (r > threshold)) {
    x = 255; // at end
  }
  else if ((l < threshold) && (c < threshold) && (r < threshold)) {
    x = -255; // off entirely
  }
  else if ((l > threshold) && (c > threshold) && (r > threshold)){
    x = 255;
  }
  else {
    x = 0;
  }
  return x;
}

int find_distance(void) {
  int sum = 0;
  int N = 1;
  for (int i = 0; i < N; i++) {
    pinMode(DIST_SENSOR_PIN, OUTPUT);
    digitalWrite(DIST_SENSOR_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(DIST_SENSOR_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(DIST_SENSOR_PIN, LOW);
    pinMode(DIST_SENSOR_PIN, INPUT);
    long duration = pulseIn(DIST_SENSOR_PIN, HIGH);
    // According to Parallax's datasheet for the PING))), there are
    // 73.746 microseconds per inch (i.e. sound travels at 1130 feet per
    // second).  This gives the distance travelled by the ping, outbound
    // and return, so we divide by 2 to get the distance of the obstacle.
    // See: http://www.parallax.com/dl/docs/prod/acc/28015-PING-v1.3.pdf
    int distance = duration / 29 / 2; // inches
    sum = sum + distance;
  }
  Serial.println(sum / N);
  return sum / N;
}

void set_servos(int fl, int fr, int bl, int br) {
  pwm.setPWM(FRONT_LEFT_SERVO, 0, SERVO_OFF + fl + FL);
  pwm.setPWM(FRONT_RIGHT_SERVO, 0, SERVO_OFF + fr +FR);
  pwm.setPWM(BACK_LEFT_SERVO, 0, SERVO_OFF + bl + BL);
  pwm.setPWM(BACK_RIGHT_SERVO, 0, SERVO_OFF + br + BR);
}




