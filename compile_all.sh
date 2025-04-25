#!/bin/bash
###
 # @Author: Yuhua.Qi fatmoonqyp@126.com
 # @Date: 2023-10-17 20:54:52
 # @LastEditors: Yuhua.Qi fatmoonqyp@126.com
 # @LastEditTime: 2023-10-30 13:50:15
 # @FilePath: /Prometheus/compile_all.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 
# 脚本名称: compile_all.sh
# 脚本描述: 编译Prometheus所有开源模块

# 编译基础模块
catkin_make --source Modules/common --build build/common

# 编译通信模块
catkin_make --source Modules/communication --build build/communication

# 编译控制模块
catkin_make --source Modules/uav_control --build build/uav_control
# 编译demo模块
catkin_make --source Modules/tutorial_demo --build build/tutorial_demo

catkin_make --source Modules/Function/hover --build build/Function/hover


# 编译Gazebo仿真模块
catkin_make --source Simulator/gazebo_simulator --build build/prometheus_gazebo
catkin_make --source Simulator/realsense_gazebo_plugin --build build/realsense_gazebo_plugin
catkin_make --source Simulator/velodyne_gazebo_plugins --build build/velodyne_gazebo_plugins
catkin_make --source Simulator/livox_laser_simulation --build build/livox_laser_simulation


# 编译Fast-lio模块
catkin_make --source Modules/FAST_LIO --build build/FAST_LIO
catkin_make --source Modules/direct_lidar_inertial_odometry-feature-livox-support --build build/direct_lidar_inertial_odometry-feature-livox-support