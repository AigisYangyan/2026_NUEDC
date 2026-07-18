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
UART_VISION_DRIVER_SRC = ../../hc-team/driver/uart_vision/uart_vision.c
UART_VISION_TEST_SRC = test_uart_vision.c
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
LOST_LINE_SRC = ../../hc-team/app/service/line_follow/lost_line.c
LOST_LINE_TEST_SRC = test_lost_line.c
LINE_FOLLOW_SRC = ../../hc-team/app/service/line_follow/line_follow.c
LINE_FOLLOW_TEST_SRC = test_line_follow.c
TUNING_SRC = ../../hc-team/app/service/tuning/tuning.c
TUNING_CHASSIS_SRC = ../../hc-team/app/service/tuning/tuning_chassis.c
TUNING_TEST_SRC = test_tuning.c
HMI_SRC = ../../hc-team/app/service/hmi/hmi.c
HMI_TEST_SRC = test_hmi.c
KEY_DRIVER_SRC = ../../hc-team/driver/key/key.c
SCHEDULER_SRC = ../../hc-team/app/scheduler/scheduler.c
SCHEDULER_TEST_SRC = test_scheduler.c
MENU_SRC = ../../hc-team/app/ui/menu/menu.c
MENU_PARAM_SRC = ../../hc-team/app/ui/menu/menu_param.c
MENU_TEST_SRC = test_menu.c
HEADING_SRC = ../../hc-team/middleware/odometry/heading.c
HEADING_TEST_SRC = test_heading.c
ODOMETRY_SRC = ../../hc-team/middleware/odometry/odometry.c
ODOMETRY_TEST_SRC = test_odometry.c
MOTION_SRC = ../../hc-team/app/service/motion/motion.c
MOTION_TEST_SRC = test_motion.c
TRACK_ELEMENTS_SRC = ../../hc-team/middleware/track_elements/track_elements.c
TRACK_ELEMENTS_TEST_SRC = test_track_elements.c
SPEED_PLAN_SRC = ../../hc-team/middleware/speed_plan/speed_plan.c
SPEED_PLAN_TEST_SRC = test_speed_plan.c
ROUTE_SRC = ../../hc-team/app/service/route/route.c
ROUTE_TEST_SRC = test_route.c

all: test_encoder run test_pid run_pid test_motor run_motor test_key run_key test_uart_fifo run_uart_fifo test_vofa_rx run_vofa_rx test_stepmotor_uart run_stepmotor_uart test_uart_vision run_uart_vision test_oled run_oled test_imu run_imu test_gray run_gray test_track_error run_track_error test_chassis run_chassis test_lost_line run_lost_line test_line_follow run_line_follow test_tuning run_tuning test_hmi run_hmi test_scheduler run_scheduler test_menu run_menu test_heading run_heading test_odometry run_odometry test_motion run_motion test_track_elements run_track_elements test_speed_plan run_speed_plan test_route run_route

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

test_uart_vision: $(UART_VISION_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(UART_VISION_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(UART_VISION_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(UART_VISION_TEST_SRC) $(GC_LDFLAGS) -lm

run_uart_vision: test_uart_vision
	./test_uart_vision

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

test_lost_line: $(LOST_LINE_SRC) ../../hc-team/app/service/line_follow/lost_line.h $(LOST_LINE_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(LOST_LINE_SRC) $(LOST_LINE_TEST_SRC) $(GC_LDFLAGS) -lm

run_lost_line: test_lost_line
	./test_lost_line

test_line_follow: $(LINE_FOLLOW_SRC) $(LOST_LINE_SRC) $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(TRACK_ERROR_SRC) $(TRACK_ELEMENTS_SRC) $(SPEED_PLAN_SRC) $(GRAY_DRIVER_SRC) $(FAKE_GRAY_PORT_SRC) $(FAKE_CLOCK_SRC) $(LINE_FOLLOW_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(LINE_FOLLOW_SRC) $(LOST_LINE_SRC) $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(TRACK_ERROR_SRC) $(TRACK_ELEMENTS_SRC) $(SPEED_PLAN_SRC) $(GRAY_DRIVER_SRC) $(FAKE_GRAY_PORT_SRC) $(FAKE_CLOCK_SRC) $(LINE_FOLLOW_TEST_SRC) $(GC_LDFLAGS) -lm

run_line_follow: test_line_follow
	./test_line_follow

test_tuning: $(TUNING_SRC) $(TUNING_CHASSIS_SRC) ../../hc-team/app/service/tuning/tuning.h ../../hc-team/app/service/tuning/tuning_chassis.h $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(FAKE_CLOCK_SRC) $(VOFA_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(TUNING_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(TUNING_SRC) $(TUNING_CHASSIS_SRC) $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(FAKE_CLOCK_SRC) $(VOFA_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(TUNING_TEST_SRC) $(GC_LDFLAGS) -lm

run_tuning: test_tuning
	./test_tuning

test_hmi: $(HMI_SRC) ../../hc-team/app/service/hmi/hmi.h $(KEY_DRIVER_SRC) $(FAKE_SRC) $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(HMI_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(HMI_SRC) $(KEY_DRIVER_SRC) $(FAKE_SRC) $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(HMI_TEST_SRC) $(GC_LDFLAGS) -lm

run_hmi: test_hmi
	./test_hmi

test_scheduler: $(SCHEDULER_SRC) ../../hc-team/app/scheduler/scheduler.h $(SCHEDULER_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(SCHEDULER_SRC) $(SCHEDULER_TEST_SRC) $(GC_LDFLAGS)

run_scheduler: test_scheduler
	./test_scheduler

test_menu: $(MENU_SRC) $(MENU_PARAM_SRC) ../../hc-team/app/ui/menu/menu.h ../../hc-team/app/ui/menu/menu_param.h $(HMI_SRC) $(KEY_DRIVER_SRC) $(FAKE_SRC) $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(SCHEDULER_SRC) $(MENU_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(MENU_SRC) $(MENU_PARAM_SRC) $(HMI_SRC) $(KEY_DRIVER_SRC) $(FAKE_SRC) $(OLED_DRIVER_SRC) $(FAKE_I2C_PORT_SRC) $(SCHEDULER_SRC) $(MENU_TEST_SRC) $(GC_LDFLAGS) -lm

run_menu: test_menu
	./test_menu

test_heading: $(HEADING_SRC) ../../hc-team/middleware/odometry/heading.h $(HEADING_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(HEADING_SRC) $(HEADING_TEST_SRC) $(GC_LDFLAGS) -lm

run_heading: test_heading
	./test_heading

test_odometry: $(ODOMETRY_SRC) $(HEADING_SRC) ../../hc-team/middleware/odometry/odometry.h ../../hc-team/middleware/odometry/heading.h $(ODOMETRY_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(ODOMETRY_SRC) $(HEADING_SRC) $(ODOMETRY_TEST_SRC) $(GC_LDFLAGS) -lm

run_odometry: test_odometry
	./test_odometry

test_motion: $(MOTION_SRC) ../../hc-team/app/service/motion/motion.h $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(ODOMETRY_SRC) $(HEADING_SRC) $(IMU_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(FAKE_CLOCK_SRC) $(MOTION_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(MOTION_SRC) $(CHASSIS_SRC) $(DRIVER_SRC) $(FAKE_SRC) $(MOTOR_DRIVER_SRC) $(MOTOR_FAKE_SRC) $(PID_SRC) $(ODOMETRY_SRC) $(HEADING_SRC) $(IMU_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_UART_PORT_SRC) $(FAKE_CLOCK_SRC) $(MOTION_TEST_SRC) $(GC_LDFLAGS) -lm

run_motion: test_motion
	./test_motion

test_track_elements: $(TRACK_ELEMENTS_SRC) ../../hc-team/middleware/track_elements/track_elements.h $(TRACK_ELEMENTS_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(TRACK_ELEMENTS_SRC) $(TRACK_ELEMENTS_TEST_SRC) $(GC_LDFLAGS) -lm

run_track_elements: test_track_elements
	./test_track_elements

test_speed_plan: $(SPEED_PLAN_SRC) ../../hc-team/middleware/speed_plan/speed_plan.h $(SPEED_PLAN_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(SPEED_PLAN_SRC) $(SPEED_PLAN_TEST_SRC) $(GC_LDFLAGS) -lm

run_speed_plan: test_speed_plan
	./test_speed_plan

# route = line_follow ∪ motion 两链接集之并集（契约 §20.6）；链 fake_clock 不链 fake_i2c_port
test_route: $(ROUTE_SRC) ../../hc-team/app/service/route/route.h $(LINE_FOLLOW_SRC) $(LOST_LINE_SRC) $(MOTION_SRC) $(CHASSIS_SRC) $(ODOMETRY_SRC) $(HEADING_SRC) $(TRACK_ERROR_SRC) $(TRACK_ELEMENTS_SRC) $(SPEED_PLAN_SRC) $(PID_SRC) $(DRIVER_SRC) $(MOTOR_DRIVER_SRC) $(GRAY_DRIVER_SRC) $(IMU_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_GRAY_PORT_SRC) $(FAKE_SRC) $(MOTOR_FAKE_SRC) $(FAKE_UART_PORT_SRC) $(FAKE_CLOCK_SRC) $(ROUTE_TEST_SRC)
	$(CC) $(CFLAGS) -o $@ $(ROUTE_SRC) $(LINE_FOLLOW_SRC) $(LOST_LINE_SRC) $(MOTION_SRC) $(CHASSIS_SRC) $(ODOMETRY_SRC) $(HEADING_SRC) $(TRACK_ERROR_SRC) $(TRACK_ELEMENTS_SRC) $(SPEED_PLAN_SRC) $(PID_SRC) $(DRIVER_SRC) $(MOTOR_DRIVER_SRC) $(GRAY_DRIVER_SRC) $(IMU_DRIVER_SRC) $(BOARD_UART_VISION_SRC) $(BOARD_UART_VOFA_SRC) $(BOARD_UART_STEPMOTOR_SRC) $(BOARD_UART_IMU_SRC) $(FAKE_GRAY_PORT_SRC) $(FAKE_SRC) $(MOTOR_FAKE_SRC) $(FAKE_UART_PORT_SRC) $(FAKE_CLOCK_SRC) $(ROUTE_TEST_SRC) $(GC_LDFLAGS) -lm

run_route: test_route
	./test_route

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
	cmd.exe /D /C "if exist test_uart_vision.exe del /Q /F test_uart_vision.exe"
	cmd.exe /D /C "if exist test_uart_vision del /Q /F test_uart_vision"
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
	cmd.exe /D /C "if exist test_lost_line.exe del /Q /F test_lost_line.exe"
	cmd.exe /D /C "if exist test_lost_line del /Q /F test_lost_line"
	cmd.exe /D /C "if exist test_line_follow.exe del /Q /F test_line_follow.exe"
	cmd.exe /D /C "if exist test_line_follow del /Q /F test_line_follow"
	cmd.exe /D /C "if exist test_tuning.exe del /Q /F test_tuning.exe"
	cmd.exe /D /C "if exist test_tuning del /Q /F test_tuning"
	cmd.exe /D /C "if exist test_hmi.exe del /Q /F test_hmi.exe"
	cmd.exe /D /C "if exist test_hmi del /Q /F test_hmi"
	cmd.exe /D /C "if exist test_scheduler.exe del /Q /F test_scheduler.exe"
	cmd.exe /D /C "if exist test_scheduler del /Q /F test_scheduler"
	cmd.exe /D /C "if exist test_menu.exe del /Q /F test_menu.exe"
	cmd.exe /D /C "if exist test_menu del /Q /F test_menu"
	cmd.exe /D /C "if exist test_heading.exe del /Q /F test_heading.exe"
	cmd.exe /D /C "if exist test_heading del /Q /F test_heading"
	cmd.exe /D /C "if exist test_odometry.exe del /Q /F test_odometry.exe"
	cmd.exe /D /C "if exist test_odometry del /Q /F test_odometry"
	cmd.exe /D /C "if exist test_motion.exe del /Q /F test_motion.exe"
	cmd.exe /D /C "if exist test_motion del /Q /F test_motion"
	cmd.exe /D /C "if exist test_track_elements.exe del /Q /F test_track_elements.exe"
	cmd.exe /D /C "if exist test_track_elements del /Q /F test_track_elements"
	cmd.exe /D /C "if exist test_speed_plan.exe del /Q /F test_speed_plan.exe"
	cmd.exe /D /C "if exist test_speed_plan del /Q /F test_speed_plan"
	cmd.exe /D /C "if exist test_route.exe del /Q /F test_route.exe"
	cmd.exe /D /C "if exist test_route del /Q /F test_route"
else
clean:
	rm -f test_encoder test_encoder.exe test_pid test_pid.exe test_motor test_motor.exe test_key test_key.exe test_uart_fifo test_uart_fifo.exe test_vofa_rx test_vofa_rx.exe test_stepmotor_uart test_stepmotor_uart.exe test_uart_vision test_uart_vision.exe test_oled test_oled.exe test_imu test_imu.exe test_gray test_gray.exe test_track_error test_track_error.exe test_chassis test_chassis.exe test_lost_line test_lost_line.exe test_line_follow test_line_follow.exe test_tuning test_tuning.exe test_hmi test_hmi.exe test_scheduler test_scheduler.exe test_menu test_menu.exe test_heading test_heading.exe test_odometry test_odometry.exe test_motion test_motion.exe test_track_elements test_track_elements.exe test_speed_plan test_speed_plan.exe test_route test_route.exe
endif

.PHONY: all run run_pid run_motor run_key run_uart_fifo run_vofa_rx run_stepmotor_uart run_uart_vision run_oled run_imu run_gray run_track_error run_chassis run_lost_line run_line_follow run_tuning run_hmi run_scheduler run_menu run_heading run_odometry run_motion run_track_elements run_speed_plan run_route clean
