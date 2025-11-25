#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include "driver/pcnt.h"

#define BRAKE 0
#define CW    1
#define CCW   2

// MOTOR R (RIGHT)
#define MOTOR_RA_PIN 4
#define MOTOR_RB_PIN 27

// MOTOR L (LEFT)
#define MOTOR_LA_PIN 32
#define MOTOR_LB_PIN 33

#define PWM_MOTOR_R 25
#define PWM_MOTOR_L 26

#define EN_PIN_R 19
#define EN_PIN_L 18

#define MOTOR_R 0
#define MOTOR_L 1

#define ENCODER_RA 14  // Pino do canal A do encoder do Motor R
#define ENCODER_RB 12  // Pino do canal B do encoder do Motor R

#define ENCODER_LA 16  // Pino do canal A do encoder do Motor L
#define ENCODER_LB 17  // Pino do canal B do encoder do Motor L

#define PULSOS_POR_VOLTA 11

enum MotionCommand {
  MOTION_STOP = 0,
  MOTION_FORWARD,
  MOTION_REVERSE,
  MOTION_TURN_LEFT,
  MOTION_TURN_RIGHT,
};

void setupPCNT();

void setupMotor();
void encoder();
void Stop();
void Forward(uint8_t usSpeedR, uint8_t usSpeedL);
void Reverse(uint8_t usSpeedR, uint8_t usSpeedL);
void TurnLeft(uint8_t usSpeedR, uint8_t usSpeedL);
void TurnRight(uint8_t usSpeedR, uint8_t usSpeedL);
void Lock();

void set_remote_motion_command(MotionCommand command);
MotionCommand get_remote_motion_command();
void apply_motion_command(MotionCommand command);

void motorGo(uint8_t motor, uint8_t direct, uint8_t pwm);

extern bool block_foward;
extern bool block_reverse;

#endif
