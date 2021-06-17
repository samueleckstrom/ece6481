#include <mbed.h>
#include "stdint.h"
#include "cmsis_gcc.h"
#include "samd21.h"

DigitalOut blue_led(PA20);
DigitalOut red_led(PA21);
DigitalOut white_led(PB15);
DigitalOut green_led(PB05);
DigitalIn button(PB06);

I2C i2c(PA08, PA09);

#define slave_address_0x38 0x38  // This defines one of the two slave addresses for MMA8451
#define slave_address_0x3A 0x3A  // This defines one of the two slave addresses for MMA8451
// The address MMA8451 is the slave address I used for this project
#define data_status 0x00  // This defines the register that indicates whether new data is ready to be collected
#define out_x_msb 0x01  // This defines the register that holds the most significant bits for acceleration in the x direction
// Note that this is also the starting point for multi byte reads of the acceleration data
// Therefore, you only need this register to read acceleration in all directions - I never want only the x data
#define who_am_i 0x0D  // This defines the register that holds the id for the MMA8451
// This register is only used to authenticate the MMA8451 is being used correctly
#define ctrl_reg1 0x2A  // This defines the register that changes sampling rate and STANDBY/ACTIVE modes

// Variables used within the main function
bool user_wants_to_create_new_password;
bool user_has_created_password;
bool user_wants_to_unlock;

// Variables used within the createNewPassword function
// These are the accelerations of the lock pattern
float lock_data_x[200];
float lock_data_y[200];
float lock_data_z[200];

// Variables used within the attemptToUnlock function
// These are the accelerations of the unlock pattern
float unlock_data_x[200];
float unlock_data_y[200];
float unlock_data_z[200];
int number_of_correct_gestures;  // Used to record how many accelerations between lock and unlock are similar

// Variables used within the readAccelerationData function
int ack_or_nack;
char register_data_status[1];
char register_out_x_msb[1];
char register_who_am_i[1];
char register_ctrl_reg1[2];
char data_ready[1];
char id_correct[1];
char data[6];  // This is the array that I use to store the temporary acceleration bits
short merged_MSB_and_LSB_from_data[3];  // This is the array that combines LSB with MSB
float acceleration_x, acceleration_y, acceleration_z;  // Ranges from ~ -39 m/s^2 to 39 m/s^2

// Variables used within the buttonPushedTime function
int button_pushed_time;

// Setup all the necessary variables, registers, etc that are required to make this 
// program work
void setup() {
  user_wants_to_create_new_password = false;
  user_has_created_password = false;
  register_data_status[0] = data_status;
  register_out_x_msb[0] = out_x_msb;
  register_who_am_i[0] = who_am_i;
  register_ctrl_reg1[0] = ctrl_reg1;
  register_ctrl_reg1[1] = 0x31;  // This sets the mode to ACTIVE and a data rate of 50 Hz
  i2c.frequency(100000);
}

// Change from STANDBY to ACTIVE mode and change sampling rate to 50 Hz
// This function is required to be able to collect data!!!
void changeToActiveMode() {
  i2c.start();
  ack_or_nack = i2c.write(slave_address_0x3A, register_ctrl_reg1, 2, false);
  if (ack_or_nack != 0) {
    red_led = 1;
  }
  i2c.stop();
}

// Quick burst from the blue led to show that the pattern time has elapsed
void blinkTwiceToShowCompletedMotion () {
  blue_led = 1;
  wait_ms(50);
  blue_led = 0;
  wait_ms(200);
  blue_led = 1;
  wait_ms(50);
  blue_led = 0;
}

// There are two device addresses 0011100 and 0011101
// The read and writes for address 0011100 are 0x39 for read and 0x38 for write
// The read and writes for address 0011101 are 0x3B for read and 0x3A for write
// This function reads the data from registers 0x01 to 0x06
// These registers correspond to the acceleration data
// Int function that returns -1 if there is an error and 0 if it is successful
int readAccelerationData() {
  i2c.start();
  ack_or_nack = i2c.write(slave_address_0x3A, register_out_x_msb, 1, true);
  if (ack_or_nack != 0) {
    return -1;
  }
  ack_or_nack = i2c.read(slave_address_0x3A, data, 6, false);
  if (ack_or_nack != 0) {
    return -1;
  }
  i2c.stop();

  merged_MSB_and_LSB_from_data[0] = (data[0] << 8) | data[1];
  merged_MSB_and_LSB_from_data[1] = (data[2] << 8) | data[3];
  merged_MSB_and_LSB_from_data[2] = (data[4] << 8) | data[5];

  // Must divide by 2048.0 because the current g range is -4 to +4g
  // If the range is changed, then the float will also have to be changed
  // Divide by 1024.0 if the g range is -8 to 8 and 4096.0 if the g range is -2 to 2
  acceleration_x = merged_MSB_and_LSB_from_data[0] / 2048.0;
  acceleration_y = merged_MSB_and_LSB_from_data[1] / 2048.0;
  acceleration_z = merged_MSB_and_LSB_from_data[2] / 2048.0;
}

// The user wants to create a new password
// This can either be because they have not yet created a password or they want a new password
// The user can achieve this by holding the button for 3 seconds or longer
void createNewPassword() {
  for (int i = 0; i < 6; i++) {
    blue_led = !blue_led;
    wait_ms(500);
  }
  
  // Loop that records the motion from the user pattern - stores in lock
  for (int i = 0; i < 200; i++) {
    int error = readAccelerationData();
    if (error == -1) {
      return;
    }
    lock_data_x[i] = acceleration_x;
    lock_data_y[i] = acceleration_y;
    lock_data_z[i] = acceleration_z;
    wait_ms(20);
  }
  
  user_wants_to_create_new_password = false;
  user_has_created_password = true;
  
  blinkTwiceToShowCompletedMotion();
}

// The user wants to try to unlock the device
// This can only be accomplished if the user has already created a password
// The user can try to unlock the password by holding the button for three seconds or less
void attemptToUnlock() {
  for (int i = 0; i < 26; i++) {
    blue_led = !blue_led;
    wait_ms(100);
  }
  wait_ms(400);

  // Loop that records the motion from the user pattern - stores in key array
  for (int i = 0; i < 200; i++) {
    int error = readAccelerationData();
    if (error == -1) {
      return;
    }
    unlock_data_x[i] = acceleration_x;
    unlock_data_y[i] = acceleration_y;
    unlock_data_z[i] = acceleration_z;
    wait_ms(20);
  }

  // This doozy of a loop determines if the motion from the user was a close enough match to unlock the device
  // The average algorithm is: key_lock * 0.7 - 0.75 < key_value < key_lock * 1.4 + 0.55
  // If the key value is within a certain range of the lock, then the device will 'unlock'
  number_of_correct_gestures = 0;
  for (int i = 0; i < 200; i++) {
    if (lock_data_x[i] >= 0) {
      if (lock_data_y[i] >= 0) {
        if (lock_data_z[i] >=0) {
          if (unlock_data_x[i] < 1.4 * lock_data_x[i] + 0.75 && unlock_data_x[i] > 0.7 * lock_data_x[i] - 0.75 &&
              unlock_data_y[i] < 1.4 * lock_data_y[i] + 0.75 && unlock_data_y[i] > 0.7 * lock_data_y[i] - 0.75 &&
              unlock_data_z[i] < 1.4 * lock_data_z[i] + 0.75 && unlock_data_z[i] > 0.7 * lock_data_z[i] - 0.75) {
              number_of_correct_gestures++;
            }
        } else {
          if (unlock_data_x[i] < 1.4 * lock_data_x[i] + 0.75 && unlock_data_x[i] > 0.7 * lock_data_x[i] - 0.75 &&
              unlock_data_y[i] < 1.4 * lock_data_y[i] + 0.75 && unlock_data_y[i] > 0.7 * lock_data_y[i] - 0.75 &&
              unlock_data_z[i] > 1.4 * lock_data_z[i] - 0.75 && unlock_data_z[i] < 0.7 * lock_data_z[i] + 0.75) {
              number_of_correct_gestures++;
            }
        }
      } else {
        if (lock_data_z[i] >=0) {
          if (unlock_data_x[i] < 1.4 * lock_data_x[i] + 0.75 && unlock_data_x[i] > 0.7 * lock_data_x[i] - 0.75 &&
              unlock_data_y[i] > 1.4 * lock_data_y[i] - 0.75 && unlock_data_y[i] < 0.7 * lock_data_y[i] + 0.75 &&
              unlock_data_z[i] < 1.4 * lock_data_z[i] + 0.75 && unlock_data_z[i] > 0.7 * lock_data_z[i] - 0.75) {
              number_of_correct_gestures++;
            }
        } else {
          if (unlock_data_x[i] < 1.4 * lock_data_x[i] + 0.75 && unlock_data_x[i] > 0.7 * lock_data_x[i] - 0.75 &&
              unlock_data_y[i] > 1.4 * lock_data_y[i] - 0.75 && unlock_data_y[i] < 0.7 * lock_data_y[i] + 0.75 &&
              unlock_data_z[i] > 1.4 * lock_data_z[i] - 0.75 && unlock_data_z[i] < 0.7 * lock_data_z[i] + 0.75) {
              number_of_correct_gestures++;
            }
        }
      }
    } else {
      if (lock_data_y[i] >= 0) {
        if (lock_data_z[i] >=0) {
          if (unlock_data_x[i] > 1.4 * lock_data_x[i] - 0.75 && unlock_data_x[i] < 0.7 * lock_data_x[i] + 0.75 &&
              unlock_data_y[i] < 1.4 * lock_data_y[i] + 0.75 && unlock_data_y[i] > 0.7 * lock_data_y[i] - 0.75 &&
              unlock_data_z[i] < 1.4 * lock_data_z[i] + 0.75 && unlock_data_z[i] > 0.7 * lock_data_z[i] - 0.75) {
              number_of_correct_gestures++;
            }
        } else {
          if (unlock_data_x[i] > 1.4 * lock_data_x[i] - 0.75 && unlock_data_x[i] < 0.7 * lock_data_x[i] + 0.75 &&
              unlock_data_y[i] < 1.4 * lock_data_y[i] + 0.75 && unlock_data_y[i] > 0.7 * lock_data_y[i] - 0.75 &&
              unlock_data_z[i] > 1.4 * lock_data_z[i] - 0.75 && unlock_data_z[i] < 0.7 * lock_data_z[i] + 0.75) {
              number_of_correct_gestures++;
            }
        }
      } else {
        if (lock_data_z[i] >=0) {
          if (unlock_data_x[i] > 1.4 * lock_data_x[i] - 0.75 && unlock_data_x[i] < 0.7 * lock_data_x[i] + 0.75 &&
              unlock_data_y[i] > 1.4 * lock_data_y[i] - 0.75 && unlock_data_y[i] < 0.7 * lock_data_y[i] + 0.75 &&
              unlock_data_z[i] < 1.4 * lock_data_z[i] + 0.75 && unlock_data_z[i] > 0.7 * lock_data_z[i] - 0.75) {
              number_of_correct_gestures++;
            }
        } else {
          if (unlock_data_x[i] > 1.4 * lock_data_x[i] - 0.75 && unlock_data_x[i] < 0.7 * lock_data_x[i] + 0.75 &&
              unlock_data_y[i] > 1.4 * lock_data_y[i] - 0.75 && unlock_data_y[i] < 0.7 * lock_data_y[i] + 0.75 &&
              unlock_data_z[i] > 1.4 * lock_data_z[i] - 0.75 && unlock_data_z[i] < 0.7 * lock_data_z[i] + 0.75) {
              number_of_correct_gestures++;
            }
        }
      }
    }
  }
  
  user_wants_to_unlock = false;

  blinkTwiceToShowCompletedMotion();

  // If the motion is correct, then blick the green LED three times
  // If the motion is incorrect, then blink the red LED three times
  if (number_of_correct_gestures > 100) {
    for (int i = 0; i < 6; i++) {
      green_led = !green_led;
      wait_ms(500);
    }
  } else {
    for (int i = 0; i < 6; i++) {
      red_led = !red_led;
      wait_ms(500);
    }
  }
}

// Validates that read and write are correct by checking the device id
// Not overwhelmingly helpful for the current objective, but a great way to make sure youe
// hardware is working properly
// Turns on the red led if something is wrong
void testsToCheckThatIdIsCorrect() {
  i2c.start();
  ack_or_nack = i2c.write(slave_address_0x3A, register_who_am_i, 1, true);
  ack_or_nack = i2c.read(slave_address_0x3A, id_correct, 1, false);
  i2c.stop();
  if ((int) id_correct[0] != 26) {
    red_led = 1;
  }
}

// Examines the status register to determine if new acceleration data is ready to be collected
// Returns -1 on error, 0 on no new data, and 1 on new data ready
int dataIsReadyToBeCollected() {
  i2c.start();
  ack_or_nack = i2c.write(slave_address_0x3A, register_data_status, 1, true);
  if (ack_or_nack != 0) {
    return -1;
  }
  ack_or_nack = i2c.read(slave_address_0x3A, data_ready, 1, false);
  if (ack_or_nack != 0) {
    return -1;
  }

  if (data_ready[0]) {
    return 1;
  }
  return 0;
}

// This function records how long the button is held for
// If the button is held long enough, this prompts the user to enter a new code
// If the button is held less than three second, then this prompts the user to try and unlock the device
void checkButtonTimer() {
  white_led = 1;
  button_pushed_time = 0;
  while (!button) {
    button_pushed_time++;
    wait_ms(10);
  }
  white_led = 0;
  if (button_pushed_time > 300) {
    user_wants_to_create_new_password = true;
  } else if (user_has_created_password) {
    user_wants_to_unlock = true;
  }
}

// Simple function to determine if all the LEDs are working properly
// Also gives a little to time to startup the MMA8451 without using a wait
void flashLEDs() {
  white_led = 1;
  wait(1);
  red_led = 1;
  wait(1);
  white_led = 0;
  green_led = 1;
  wait(1);
  red_led = 0;
  blue_led = 1;
  wait(1);
  green_led = 0;
  wait(1);
  blue_led = 0;
}

// Main loop that brings together everything
// Uses polling to determine if the button was pressed
int main() {
  setup();
  testsToCheckThatIdIsCorrect();
  changeToActiveMode();
  flashLEDs();

  while (1) {
    if (!button) {
      checkButtonTimer();
    }

    if (user_wants_to_create_new_password) {
      createNewPassword();
    }

    if (user_wants_to_unlock) {
      attemptToUnlock();
    }
  }
}