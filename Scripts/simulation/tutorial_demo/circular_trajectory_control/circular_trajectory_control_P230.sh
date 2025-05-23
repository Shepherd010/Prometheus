#!/bin/bash
# 脚本名称: circular_trajectory_control
# 脚本描述: 该脚本为本地坐标系下速度控制demo启动脚本,包含PX4 SITL,Gazebo仿真环境,无人机控制节点以及本地坐标系下速度控制节点

gnome-terminal --window -e 'bash -c "roscore; exec bash"' \
--tab -e 'bash -c "sleep 5; roslaunch prometheus_gazebo sitl_indoor_1uav_P230.launch; exec bash"' \
--tab -e 'bash -c "sleep 6; roslaunch prometheus_uav_control uav_control_main_indoor.launch; exec bash"' \
--tab -e 'bash -c "sleep 14; roslaunch prometheus_demo circular_trajectory_control.launch; exec bash"' \
#--tab -e 'bash -c "sleep 7; rosrun prometheus_demo circular_trajectory_control.py; exec bash"' \
