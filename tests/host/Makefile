# Minimal host test makefile for Encoder Driver.
# Compiles with the system C compiler; does not link any MSPM0 HAL.
# User-specified clean: cmd.exe /D /C "if exist ... del /Q /F".

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections -I../../hc-team -I../..
GC_LDFLAGS = -Wl,--gc-sections

DRIVER_SRC = ../../hc-team/driver/encoder/encoder.c
ENCODER_OWNER_SRC = ../../hc-team/app/tasks/task_groups.c
FAKE_SRC = fake_board_gpio.c
TEST_SRC = test_encoder.c
PID_SRC = ../../hc-team/middleware/pid/pid.c
PID_TEST_SRC = test_pid.c
MOTOR_TEST_SRC = test_motor.c
MOTOR_DRIVER_SRC = ../../hc-team/driver/motor/motor.c
MOTOR_FAKE_SRC = fake_motor_hw.c
KEY_TEST_SRC = test_key.c

all: test_encoder run test_pid run_pid test_motor run_motor test_key run_key

test_encoder: $(DRIVER_SRC) $(ENCODER_OWNER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(DRIVER_SRC) $(ENCODER_OWNER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(TEST_SRC) $(GC_LDFLAGS) -lm

run: test_encoder
	./test_encoder

test_pid: $(PID_SRC) $(PID_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(PID_SRC) $(PID_TEST_SRC) -lm

run_pid: test_pid
	./test_pid

test_motor: $(MOTOR_DRIVER_SRC) ../../hc-team/driver/motor/motor.h ../../hc-team/driver/motor/motor_hw.h $(MOTOR_FAKE_SRC) $(MOTOR_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(MOTOR_TEST_SRC) $(GC_LDFLAGS) -lm

run_motor: test_motor
	./test_motor

test_key: ../../hc-team/driver/key/key.c ../../hc-team/driver/key/key.h ../../hc-team/driver/board_gpio/board_gpio.h $(FAKE_SRC) $(KEY_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ ../../hc-team/driver/key/key.c $(FAKE_SRC) $(KEY_TEST_SRC) $(GC_LDFLAGS) -lm

run_key: test_key
	./test_key

ifeq ($(OS),Windows_NT)
clean:
	cmd.exe /D /C "if exist test_encoder.exe del /Q /F test_encoder.exe"
	cmd.exe /D /C "if exist test_encoder del /Q /F test_encoder"
	cmd.exe /D /C "if exist test_pid.exe del /Q /F test_pid.exe"
	cmd.exe /D /C "if exist test_pid del /Q /F test_pid"
	cmd.exe /D /C "if exist test_motor.exe del /Q /F test_motor.exe"
	cmd.exe /D /C "if exist test_motor del /Q /F test_motor"
	cmd.exe /D /C "if exist test_key.exe del /Q /F test_key.exe"
	cmd.exe /D /C "if exist test_key del /Q /F test_key"
else
clean:
	rm -f test_encoder test_encoder.exe test_pid test_pid.exe test_motor test_motor.exe test_key test_key.exe
endif

.PHONY: all run run_pid run_motor run_key clean
