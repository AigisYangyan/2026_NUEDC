# Minimal host test makefile for Encoder Driver.
# Compiles with the system C compiler; does not link any MSPM0 HAL.
# User-specified clean: cmd.exe /D /C "if exist ... del /Q /F".

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections -DHOST_TEST -I../../hc-team -I../..
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
FAKE_UART_PORT_SRC = fake_uart_port.c
BOARD_UART_VISION_SRC = ../../hc-team/driver/board_uart/vision_uart.c
BOARD_UART_VOFA_SRC = ../../hc-team/driver/board_uart/vofa_uart.c
BOARD_UART_STEPMOTOR_SRC = ../../hc-team/driver/board_uart/stepmotor_uart.c
VOFA_DRIVER_SRC = ../../hc-team/driver/uart_vofa/uart_vofa.c
UART_FIFO_TEST_SRC = test_uart_fifo.c
VOFA_RX_TEST_SRC = test_vofa_rx.c
STEPMOTOR_UART_TEST_SRC = test_stepmotor_uart.c
OLED_DRIVER_SRC = ../../hc-team/driver/oled/oled_hardware_i2c.c
OLED_TEST_SRC = test_oled.c
FAKE_I2C_PORT_SRC = fake_i2c_port.c
BOARD_UART_IMU_SRC = ../../hc-team/driver/board_uart/imu_uart.c
IMU_DRIVER_SRC = ../../hc-team/driver/imu/imu.c
IMU_TEST_SRC = test_imu.c
GRAY_DRIVER_SRC = ../../hc-team/driver/gray/gray.c
FAKE_GRAY_PORT_SRC = fake_gray_port.c
GRAY_TEST_SRC = test_gray.c
TRACK_ERROR_SRC = ../../hc-team/middleware/track_error/track_error.c
TRACK_ERROR_TEST_SRC = test_track_error.c
CHASSIS_SRC = ../../hc-team/app/service/chassis/chassis.c
FAKE_CLOCK_SRC = fake_clock.c
CHASSIS_TEST_SRC = test_chassis.c

all: test_encoder run test_pid run_pid test_motor run_motor test_key run_key test_uart_fifo run_uart_fifo test_vofa_rx run_vofa_rx test_stepmotor_uart run_stepmotor_uart test_oled run_oled test_imu run_imu test_gray run_gray test_track_error run_track_error test_chassis run_chassis

test_encoder: $(DRIVER_SRC) $(ENCODER_OWNER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(DRIVER_SRC) $(ENCODER_OWNER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(TEST_SRC) $(GC_LDFLAGS) -lm

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

test_uart_fifo: $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(UART_FIFO_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(UART_FIFO_TEST_SRC) $(GC_LDFLAGS) -lm

run_uart_fifo: test_uart_fifo
	./test_uart_fifo

test_vofa_rx: $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(VOFA_DRIVER_SRC) $(FAKE_UART_PORT_SRC) $(VOFA_RX_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(VOFA_DRIVER_SRC) $(FAKE_UART_PORT_SRC) $(VOFA_RX_TEST_SRC) $(GC_LDFLAGS) -lm

run_vofa_rx: test_vofa_rx
	./test_vofa_rx

test_stepmotor_uart: $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(STEPMOTOR_UART_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(STEPMOTOR_UART_TEST_SRC) $(GC_LDFLAGS) -lm

run_stepmotor_uart: test_stepmotor_uart
	./test_stepmotor_uart

test_imu: $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(IMU_DRIVER_SRC) $(FAKE_UART_PORT_SRC) $(IMU_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(IMU_DRIVER_SRC) $(FAKE_UART_PORT_SRC) $(IMU_TEST_SRC) $(GC_LDFLAGS) -lm

run_imu: test_imu
	./test_imu

test_gray: $(GRAY_DRIVER_SRC) ../../hc-team/driver/gray/gray.h ../../hc-team/driver/gray/gray_port.h $(FAKE_GRAY_PORT_SRC) $(GRAY_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(GRAY_DRIVER_SRC) $(FAKE_GRAY_PORT_SRC) $(GRAY_TEST_SRC) $(GC_LDFLAGS) -lm

run_gray: test_gray
	./test_gray

test_track_error: $(TRACK_ERROR_SRC) ../../hc-team/middleware/track_error/track_error.h $(TRACK_ERROR_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(TRACK_ERROR_SRC) $(TRACK_ERROR_TEST_SRC) $(GC_LDFLAGS) -lm

run_track_error: test_track_error
	./test_track_error

test_chassis: $(CHASSIS_SRC) ../../hc-team/app/service/chassis/chassis.h $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(FAKE_CLOCK_SRC) $(CHASSIS_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(FAKE_CLOCK_SRC) $(CHASSIS_TEST_SRC) $(GC_LDFLAGS) -lm

run_chassis: test_chassis
	./test_chassis

test_oled: $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(OLED_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(OLED_TEST_SRC) $(GC_LDFLAGS) -lm

run_oled: test_oled
	./test_oled

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
	cmd.exe /D /C "if exist test_uart_fifo.exe del /Q /F test_uart_fifo.exe"
	cmd.exe /D /C "if exist test_uart_fifo del /Q /F test_uart_fifo"
	cmd.exe /D /C "if exist test_vofa_rx.exe del /Q /F test_vofa_rx.exe"
	cmd.exe /D /C "if exist test_vofa_rx del /Q /F test_vofa_rx"
	cmd.exe /D /C "if exist test_stepmotor_uart.exe del /Q /F test_stepmotor_uart.exe"
	cmd.exe /D /C "if exist test_stepmotor_uart del /Q /F test_stepmotor_uart"
	cmd.exe /D /C "if exist test_oled.exe del /Q /F test_oled.exe"
	cmd.exe /D /C "if exist test_oled del /Q /F test_oled"
	cmd.exe /D /C "if exist test_imu.exe del /Q /F test_imu.exe"
	cmd.exe /D /C "if exist test_imu del /Q /F test_imu"
	cmd.exe /D /C "if exist test_gray.exe del /Q /F test_gray.exe"
	cmd.exe /D /C "if exist test_gray del /Q /F test_gray"
	cmd.exe /D /C "if exist test_track_error.exe del /Q /F test_track_error.exe"
	cmd.exe /D /C "if exist test_track_error del /Q /F test_track_error"
	cmd.exe /D /C "if exist test_chassis.exe del /Q /F test_chassis.exe"
	cmd.exe /D /C "if exist test_chassis del /Q /F test_chassis"
else
clean:
	rm -f test_encoder test_encoder.exe test_pid test_pid.exe test_motor test_motor.exe test_key test_key.exe test_uart_fifo test_uart_fifo.exe test_vofa_rx test_vofa_rx.exe test_stepmotor_uart test_stepmotor_uart.exe test_oled test_oled.exe test_imu test_imu.exe test_gray test_gray.exe test_track_error test_track_error.exe test_chassis test_chassis.exe
endif

.PHONY: all run run_pid run_motor run_key run_uart_fifo run_vofa_rx run_stepmotor_uart run_oled run_imu run_gray run_track_error run_chassis clean
