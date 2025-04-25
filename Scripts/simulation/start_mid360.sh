# 脚本描述: PX4 SITL Gazebo仿真环境测试

gnome-terminal --window -e 'bash -c "roscore; exec bash"' \
--tab -e 'bash -c "sleep 5; roslaunch prometheus_communication_bridge simulation_bridge.launch; exec bash"' \
--tab -e 'bash -c "sleep 6; roslaunch prometheus_gazebo sitl_indoor_1uav_P600_mid360.launch; exec bash"' \
