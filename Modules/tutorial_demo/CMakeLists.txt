cmake_minimum_required(VERSION 2.8.3)
project(prometheus_demo)

## Compile as C++11, supported in ROS Kinetic and newer
# add_compile_options(-std=c++11)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED COMPONENTS
  message_generation  
  roscpp
  rospy
  geometry_msgs
  sensor_msgs
  mavros
  nav_msgs
  std_msgs
  std_srvs
  tf2_ros
  tf2_eigen
  mavros_msgs
  prometheus_msgs
)

##  System dependencies are found with CMake's conventions
find_package(Boost REQUIRED COMPONENTS system)


################################################
## Declare ROS messages, services and actions ##
################################################

## Generate added messages and services with any dependencies listed here
generate_messages(
  DEPENDENCIES
  geometry_msgs 
  sensor_msgs
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS  message_runtime
)


###########
## Build ##
###########

## Specify additional locations of header files
## Your package locations should be listed before other locations
include_directories(
  include
  advanced/include
  ${catkin_INCLUDE_DIRS}
  ../common/include
  ../communication/include
)


###############################
##      声明可执行cpp文件     ##
###############################

add_executable(takeoff_land basic/takeoff_land/src/takeoff_land.cpp)
target_link_libraries(takeoff_land ${catkin_LIBRARIES})

add_executable(takeoff_land_no_rc basic/takeoff_land/src/takeoff_land_no_rc.cpp)
target_link_libraries(takeoff_land_no_rc ${catkin_LIBRARIES})

add_executable(global_pos_control basic/global_pos_control/src/global_pos_control.cpp)
target_link_libraries(global_pos_control ${catkin_LIBRARIES})

add_executable(enu_xyz_pos_control basic/enu_xyz_pos_control/src/enu_xyz_pos_control.cpp)
target_link_libraries(enu_xyz_pos_control ${catkin_LIBRARIES})

add_executable(body_xyz_pos_control basic/body_xyz_pos_control/src/body_xyz_pos_control.cpp)
target_link_libraries(body_xyz_pos_control ${catkin_LIBRARIES})

add_executable(circular_trajectory_control basic/circular_trajectory_control/src/circular_trajectory_control.cpp)
target_link_libraries(circular_trajectory_control ${catkin_LIBRARIES})

add_executable(waypoint_pos_control basic/waypoint_pos_control/src/waypoint_pos_control.cpp)
target_link_libraries(waypoint_pos_control ${catkin_LIBRARIES})

add_executable(formation_control advanced/formation_control/src/formation_control.cpp)
target_link_libraries(formation_control ${catkin_LIBRARIES})

add_executable(yolov5_tracking advanced/yolov5_tracking/src/yolov5_tracking.cpp)
target_link_libraries(yolov5_tracking ${catkin_LIBRARIES})

add_executable(aruco_tracking advanced/aruco_tracking/src/aruco_tracking.cpp)
target_link_libraries(aruco_tracking ${catkin_LIBRARIES})
#############
## Install ##
#############

# all install targets should use catkin DESTINATION variables
# See http://ros.org/doc/api/catkin/html/adv_user_guide/variables.html

## Mark executable scripts (Python etc.) for installation
## in contrast to setup.py, you can choose the destination
# install(PROGRAMS
  # advanced/future_aircraft/scripts/locus.py
  # DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark executables and/or libraries for installation
# install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_node
#   ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
#   RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
# )

## Mark cpp header files for installation
# install(DIRECTORY include/${PROJECT_NAME}/
#   DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
#   FILES_MATCHING PATTERN "*.h"
#   PATTERN ".svn" EXCLUDE
# )

## Mark cpp header files for installation
install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.h"
)

## Mark other files for installation (e.g. launch and bag files, etc.)
install(DIRECTORY launch/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
)

#############
## Testing ##
#############

## Add gtest based cpp test target and link libraries
# catkin_add_gtest(${PROJECT_NAME}-test test/test_prometheus_demo.cpp)
# if(TARGET ${PROJECT_NAME}-test)
#   target_link_libraries(${PROJECT_NAME}-test ${PROJECT_NAME})
# endif()

## Add folders to be run by python nosetests
# catkin_add_nosetests(test)
