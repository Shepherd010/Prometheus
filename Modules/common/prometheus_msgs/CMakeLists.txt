cmake_minimum_required(VERSION 2.8.3)
project(prometheus_msgs)

find_package(catkin REQUIRED COMPONENTS
  message_generation  
  roscpp
  geometry_msgs
  actionlib_msgs
  sensor_msgs
  nav_msgs
  std_msgs
  std_srvs
  tf2_ros
  tf2_eigen
  mavros_msgs
)

find_package(Boost REQUIRED COMPONENTS system)
find_package(PCL REQUIRED)
add_message_files(
  DIRECTORY msg
  FILES
  UAVState.msg
  MultiUAVState.msg
  UAVCommand.msg
  UAVControlState.msg
  UAVSetup.msg
  TextInfo.msg
  GlobalAruco.msg
  UGVState.msg
  UGVCommand.msg
  MultiUGVState.msg
  PositionReference.msg
  StationCommand.msg

  ArucoInfo.msg
  MultiArucoInfo.msg
  DetectionInfo.msg
  MultiDetectionInfo.msg
  BoundingBox.msg
  BoundingBoxes.msg

  SwarmCommand.msg
  FormationAssign.msg
  OffsetPose.msg
  GPSData.msg

  # UWB msg
  LinktrackNode2.msg
  LinktrackNodeframe2.msg

  #communication
  DetectionInfoSub.msg
  GimbalControl.msg
  GimbalState.msg
  MultiDetectionInfoSub.msg
  VisionDiff.msg
  WindowPosition.msg
  Bspline.msg
  MultiBsplines.msg
  Control.msg
  BasicDataTypeAndValue.msg
  CustomDataSegment.msg
  ParamSettings.msg

  #spirecv-ros msg
  ROI.msg
  Target.msg
  TargetsInFrame.msg

  #livox mid360 msg
  LivoxCustomMsg.msg
  LivoxCustomPoint.msg
)

add_action_files(
  DIRECTORY action
  FILES
  CheckForObjects.action
)

generate_messages(DEPENDENCIES
  DEPENDENCIES
  actionlib_msgs
  geometry_msgs 
  sensor_msgs
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS
  message_runtime
  actionlib_msgs
  geometry_msgs
  sensor_msgs
  message_runtime
  std_msgs
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS}
)


install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
  FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY launch/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
)
