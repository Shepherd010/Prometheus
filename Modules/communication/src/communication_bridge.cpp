#include "communication_bridge.hpp"
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include "param_manager.hpp"

CommunicationBridge::CommunicationBridge(ros::NodeHandle &nh) : Communication()
{
    // 是否仿真 1 为是  0为否
    nh.param<int>("is_simulation", this->is_simulation_, 1);

    nh.param<int>("udp_port", UDP_PORT, 8889);
    nh.param<int>("tcp_port", TCP_PORT, 55555);
    nh.param<int>("tcp_heartbeat_port", TCP_HEARTBEAT_PORT, 55556);
    // nh.param<int>("rviz_port", RVIZ_PORT, 8890);
    nh.param<int>("ROBOT_ID", ROBOT_ID, 1);
    nh.param<int>("uav_id", uav_id, 0);
    nh.param<std::string>("ground_station_ip", udp_ip, "127.0.0.1");
    nh.param<std::string>("multicast_udp_ip", multicast_udp_ip, "224.0.0.88");
    nh.param<int>("try_connect_num", try_connect_num, 3);

    nh.param<bool>("autoload", autoload, false);
    if (autoload)
    {
        nh.param<std::string>("uav_control_start", OPENUAVBASIC, "");
        nh.param<std::string>("close_uav_control", CLOSEUAVBASIC, "");
    }

    this->nh_ = nh;

    Communication::init(ROBOT_ID, UDP_PORT, TCP_PORT, TCP_HEARTBEAT_PORT);

    // thread_recCommunicationBridgeiver
    boost::thread recv_thd(&CommunicationBridge::serverFun, this);
    recv_thd.detach();        // 后台
    ros::Duration(1).sleep(); // wait

    // thread_receiver
    boost::thread recv_multicast_thd(&CommunicationBridge::multicastUdpFun, this);
    recv_multicast_thd.detach();
    ros::Duration(1).sleep(); // wait

    heartbeat.message = "OK";
    heartbeat.count = 0;


    to_ground_heartbeat_timer = nh.createTimer(ros::Duration(1.0), &CommunicationBridge::toGroundHeartbeat, this);

    heartbeat_check_timer = nh.createTimer(ros::Duration(1.0), &CommunicationBridge::checkHeartbeatState, this);
}

CommunicationBridge::~CommunicationBridge()
{
    if (this->uav_)
        this->uav_.reset();
}

// TCP服务端
void CommunicationBridge::serverFun()
{
    int valread;
    if (waitConnectionFromGroundStation(TCP_PORT) < 0)
    {
        ROS_ERROR("[bridge_node]Socket recever creation error!");
        exit(EXIT_FAILURE);
    }

    while (true)
    {
        struct sockaddr_in client_addr; // 用来存储客户端地址
        socklen_t addr_len = sizeof(client_addr);

        // 等待连接队列中抽取第一个连接，创建一个与s同类的新的套接口并返回句柄。
        if ((recv_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) < 0)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // recv函数从TCP连接的另一端接收数据
        valread = recv(recv_sock, tcp_recv_buf, BUF_LEN, 0);

        if (valread <= 0)
        {
            ROS_ERROR("Received message length <= 0, maybe connection has lost");
            close(recv_sock);
            continue;
        }

        // 开启互斥锁
        std::lock_guard<std::mutex> lg_mutex(g_mutex_);
        // 未连接的情况,以第一台连接的地面端为准
        if (!this->uav_ )
        {
            // 获取客户端的 IP 地址
            std::cout << "Client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;

            // 如果 地面站IP和组播IP一致 则将组播地址同步修改
            if (this->udp_ip == this->multicast_udp_ip)
            {
                // 设置ROS参数服务器的数据
                CommunicationBridge::setParam("multicast_udp_ip", std::string(inet_ntoa(client_addr.sin_addr)));
                multicast_udp_ip = inet_ntoa(client_addr.sin_addr);
            }
            // 设置ROS参数服务器的数据
            CommunicationBridge::setParam("ground_station_ip", std::string(inet_ntoa(client_addr.sin_addr)));
            // 设置客户端IP
            udp_ip = inet_ntoa(client_addr.sin_addr);
        }
        else if (udp_ip != inet_ntoa(client_addr.sin_addr)) // 防止多机重复抢夺控制,只允许第一台连接的无人机进行控制
        {
            // 如果此时第一台连接的地面端心跳包丢失了,允许其连接其他地面端
            if (!this->is_heartbeat_ready_)
            {
                try
                {
                    std::string message = "Original client " + udp_ip + " signal loss! new client " + std::string(inet_ntoa(client_addr.sin_addr)) + " takes over.";

                    // 如果 地面站IP和组播IP一致 则将组播地址同步修改
                    if (this->udp_ip == this->multicast_udp_ip)
                    {
                        // 设置ROS参数服务器的数据
                        CommunicationBridge::setParam("multicast_udp_ip", std::string(inet_ntoa(client_addr.sin_addr)));
                        multicast_udp_ip = inet_ntoa(client_addr.sin_addr);
                    }
                    // 设置ROS参数服务器的数据
                    CommunicationBridge::setParam("ground_station_ip", std::string(inet_ntoa(client_addr.sin_addr)));
                    udp_ip = inet_ntoa(client_addr.sin_addr);

                    // 根据实际情况实时修改
                    if (this->uav_)
                        this->uav_->setGroundStationIP(udp_ip);


                    // 获取客户端的 IP 地址
                    std::cout << "new client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;
                    sendTextInfo(TextInfo::MTG_ERROR, message);
                }
                catch (const std::exception &e)
                {
                    std::cerr << e.what() << '\n';
                }
            }
            else
            {
                std::string message = "client " + std::string(inet_ntoa(client_addr.sin_addr)) + " attempt to connect control! If the client needs to control, please disconnect the current connection.";
                sendTextInfo(TextInfo::MTG_ERROR, message);
                continue;
            }
        }

        std::cout << "tcp valread: " << valread << std::endl;
        close(recv_sock);
        pubMsg(decodeMsg(tcp_recv_buf, Send_Mode::TCP));
    }
}

void CommunicationBridge::recvData(struct UAVState uav_state)
{
}
void CommunicationBridge::recvData(struct UAVCommand uav_cmd)
{
    // 非仿真情况 只有一个UAV
    if (this->is_simulation_ == 0)
    {
        if (this->uav_)
        {
            this->uav_->uavCmdPub(uav_cmd);
        }
    }
    // 仿真情况下 可能存在多个UAV 找到对应ID进行发布对应的控制命令
    else
    {
        auto it = this->uavs_.find(recv_id);
        if (it != this->uavs_.end())
        {
            (*it).second->uavCmdPub(uav_cmd);
        }
    }
}

void CommunicationBridge::recvData(struct ConnectState connect_state)
{
    if (this->is_simulation_ == 0)
        return;
}
void CommunicationBridge::recvData(struct GimbalControl gimbal_control)
{
    // 非仿真情况 只有一个UAV
    if (this->is_simulation_ == 0)
    {
        if (this->uav_)
        {
            this->uav_->gimbalControlPub(gimbal_control);
        }
    }
    // 仿真情况下 可能存在多个UAV 找到对应ID进行发布对应的控制命令
    else
    {
        auto it = this->uavs_.find(recv_id);
        if (it != this->uavs_.end())
        {
            (*it).second->gimbalControlPub(gimbal_control);
        }
    }
}
void CommunicationBridge::recvData(struct GimbalService gimbal_service)
{
    // 非仿真情况 只有一个UAV
    if (this->is_simulation_ == 0)
    {
        if (this->uav_)
        {
            this->uav_->gimbalServer(gimbal_service);
        }
    }
    // 仿真情况下 可能存在多个UAV 找到对应ID进行发布对应的控制命令
    else
    {
        auto it = this->uavs_.find(recv_id);
        if (it != this->uavs_.end())
        {
            (*it).second->gimbalServer(gimbal_service);
        }
    }
}
void CommunicationBridge::recvData(struct GimbalParamSet param_set)
{
}
void CommunicationBridge::recvData(struct WindowPosition window_position)
{
    // 如果udp_msg数据不为空 则向udo端口发送数据。否则发布ros话题
    if (!window_position.udp_msg.empty())
    {
        std::cout << "udp_msg size :" << window_position.udp_msg.size() << std::endl;
        sendMsgByUdp(window_position.udp_msg.size(), window_position.udp_msg.c_str(), "127.0.0.1", SERV_PORT);
    }
    else
    {
        if (this->uav_ && window_position.mode == WindowPosition::Mode::POINT && window_position.track_id < 0)
        {
            this->uav_->uavTargetPub(window_position);
        }
    }
}


void CommunicationBridge::recvData(struct ImageData image_data)
{
    createImage(image_data);
}
void CommunicationBridge::recvData(struct UAVSetup uav_setup)
{
    if (this->uav_)
    {
        this->uav_->uavSetupPub(uav_setup);
    }
}
void CommunicationBridge::recvData(struct ModeSelection mode_selection)
{
    modeSwitch(mode_selection);
}
void CommunicationBridge::recvData(struct ParamSettings param_settings)
{
    if (param_settings.params.size() == 0)
    {
        switch (param_settings.param_module)
        {
        case ParamSettings::ParamModule::UAVCONTROL:
            sendControlParam();
            break;
        case ParamSettings::ParamModule::UAVCOMMUNICATION:
            sendCommunicationParam();
            break;
        case ParamSettings::ParamModule::UAVCOMMANDPUB:
            sendCommandPubParam();
            break;
        default:
            break;
        }
        return;
    }
    ParamManager p(nh_);

    // 是否是搜索
    if (param_settings.param_module == ParamSettings::ParamModule::SEARCH)
    {
        if (param_settings.params.empty())
        {
            sendTextInfo(TextInfo::MTG_INFO, "parameter search failed...");
            return;
        }
        // ParamManager p(nh_);
        std::unordered_map<std::string, std::string> param_map = p.getParams(param_settings.params.begin()->param_name);
        struct ParamSettings params;
        params.param_module = ParamSettings::ParamModule::SEARCH;
        for (const auto &pair : param_map)
        {
            struct Param param;
            param.param_name = pair.first;
            param.param_value = pair.second;
            params.params.push_back(param);
        }
        // 发送
        sendMsgByUdp(encodeMsg(Send_Mode::UDP, params), udp_ip);
        usleep(1000);
        return;
    }
    else if (param_settings.param_module == ParamSettings::ParamModule::SEARCHMODIFY)
    {
        bool set_param_flag = true;
        int reset_location_source = -1;

        for (const auto &set_param : param_settings.params)
        {
            set_param_flag = set_param_flag && p.setParam(set_param.param_name, set_param.param_value);
            if(set_param.param_name == "/uav_control_main_" + to_string(ROBOT_ID) + "/control/location_source"){
                reset_location_source = std::stoi(set_param.param_value);
            }
        }

        if (set_param_flag)
        {
            sendTextInfo(TextInfo::MTG_INFO, "parameter search modify success...");
            // 并且发布话题出来告诉其他节点
            if (this->uav_)
            {
                this->uav_->paramSettingsPub(param_settings);
                if(reset_location_source != -1){
                    // 启动定位
                }
            }
            return;
        }
        else
        {
            sendTextInfo(TextInfo::MTG_INFO, "parameter search modify failed...");
            return;
        }
    }
    else if (param_settings.param_module == ParamSettings::ParamModule::UAVCONTROL)
    {
        bool set_param_flag = true;
        std::string prefix = "/uav_control_main_" + std::to_string(getRecvID()) + "/";
        bool is_prefix = false;
        for (const auto &set_param : param_settings.params)
        {
            if (set_param.param_name[0] != '/')
                is_prefix = true;

            set_param_flag = set_param_flag && p.setParam((is_prefix ? prefix : "") + set_param.param_name, set_param.param_value);
        }

        if (set_param_flag)
        {
            sendTextInfo(TextInfo::MTG_INFO, "parameter search modify success...");
            // 并且发布话题出来告诉其他节点
            if (this->uav_)
            {
                this->uav_->paramSettingsPub(param_settings, (is_prefix ? prefix : ""));
            }
            return;
        }
        else
        {
            sendTextInfo(TextInfo::MTG_INFO, "parameter search modify failed...");
            return;
        }
    }

    if (param_settings.param_module == ParamSettings::ParamModule::UAVCOMMUNICATION)
    {
        nh_.getParam("ground_station_ip", udp_ip);
        nh_.getParam("multicast_udp_ip", multicast_udp_ip);
        nh_.getParam("is_simulation", is_simulation_);

        nh_.getParam("autoload", autoload);
        if (autoload)
        {
            nh_.getParam("uav_control_start", OPENUAVBASIC);
            nh_.getParam("close_uav_control", CLOSEUAVBASIC);
        }
        else
        {
            OPENUAVBASIC = "";
            CLOSEUAVBASIC = "";
        }

        if (this->is_heartbeat_ready_ == false)
            this->is_heartbeat_ready_ = true;
    }
}

// 此处为 地面站-->机载端 机载端<->机载端
void CommunicationBridge::recvData(struct CustomDataSegment_1 custom_data_segment)
{
    // 测试代码
    // for(int i = 0; i < custom_data_segment.datas.size();i++)
    // {
    //     struct BasicDataTypeAndValue basic = custom_data_segment.datas[i];
    //     std::cout << "类型:" << (int)basic.type << " 变量名:" << basic.name << " 变量值:" << basic.value << std::endl;
    // }

    if (this->uav_)
    {
        this->uav_->customDataSegmentPub(custom_data_segment);
    }
}

void CommunicationBridge::recvData(struct Goal goal)
{
}

// 根据协议中MSG_ID的值，将数据段数据转化为正确的结构体
void CommunicationBridge::pubMsg(int msg_id)
{
    // std::cout << "recv_id: " << getRecvID() << std::endl;
    // std::cout << "msg_id: " << msg_id << std::endl;

    switch (msg_id)
    {
    case MsgId::UAVSTATE:
        recvData(Communication::getUAVState());
        break;
    case MsgId::CONNECTSTATE:
        // 集群仿真下有效
        recvData(Communication::getConnectState());
        break;
    case MsgId::GIMBALCONTROL:
        recvData(Communication::getGimbalControl());
        break;
    case MsgId::GIMBALSERVICE:
        recvData(Communication::getGimbalService());
        break;
    case MsgId::GIMBALPARAMSET:
        recvData(Communication::getGimbalParamSet());
        break;
    case MsgId::WINDOWPOSITION:
        recvData(Communication::getWindowPosition());
        break;
    case MsgId::IMAGEDATA:
        recvData(Communication::getImageData());
        break;
    case MsgId::UAVCOMMAND:
        recvData(Communication::getUAVCommand());
        break;
    case MsgId::UAVSETUP:
        recvData(Communication::getUAVSetup());
        break;
    case MsgId::MODESELECTION:
        recvData(Communication::getModeSelection());
        break;
    case MsgId::PARAMSETTINGS:
        recvData(Communication::getParamSettings());
        break;
    case MsgId::CUSTOMDATASEGMENT_1:
        recvData(Communication::getCustomDataSegment_1());
        break;
    case MsgId::GOAL:
        recvData(Communication::getGoal());
        break;
    default:
        break;
    }
}

void CommunicationBridge::createImage(struct ImageData image_data)
{
    std::ofstream os(image_data.name);
    os << image_data.data;
    os.close();
    std::cout << "image_data" << std::endl;
}

void CommunicationBridge::modeSwitch(struct ModeSelection mode_selection)
{
    if (mode_selection.mode == ModeSelection::Mode::REBOOTNX)
    {
        system(REBOOTNXCMD);
    }
    else if (mode_selection.mode == ModeSelection::Mode::EXITNX)
    {
        system(EXITNXCMD);
    }

    if (mode_selection.use_mode == ModeSelection::UseMode::UM_CREATE)
    {
        createMode(mode_selection);
    }
    else if (mode_selection.use_mode == ModeSelection::UseMode::UM_DELETE)
    {
        deleteMode(mode_selection);
    }
}

void CommunicationBridge::createMode(struct ModeSelection mode_selection)
{
    if (mode_selection.mode == ModeSelection::Mode::UAVBASIC)
    {
        // 仿真模式 允许同一通信节点创建多个飞机的话题
        if (this->is_simulation_ == 1)
        {
            for (int i = 0; i < mode_selection.selectId.size(); i++)
            {
                int key = mode_selection.selectId[i];
                // 判断是否存在该键
                if (this->uavs_.find(key) != this->uavs_.end()) // 表示已经存在
                {
                    sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "UAV" + to_string(key) + " duplicate connections!!!");
                    continue;
                }
                // 不存在该键则创建加入
                this->uavs_.insert({key, std::make_shared<UAVBasic>(this->nh_, key, (Communication *)this)});

                if (ROBOT_ID == key) // 如果key等于本地设置的id则 再次赋值
                {
                    this->uav_ = this->uavs_[key];
                    // 打开
                    if (autoload)
                        system(OPENUAVBASIC.c_str());
                }
                sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "Simulation UAV" + to_string(mode_selection.selectId[i]) + " connection succeeded!!!");
            }
        }
        // 真机模式 同一通信节点只能创建一个飞机的话题
        else
        {
            for (int i = 0; i < mode_selection.selectId.size(); i++)
            {
                int key = mode_selection.selectId[i];
                if (ROBOT_ID == key) // 真机，必然ID匹配
                {
                    // 增加互斥锁
                    // this->is_heartbeat_ready_ = true;
                    if (this->uav_)
                    {
                        // 已经存在
                        sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "UAV" + to_string(ROBOT_ID) + " duplicate connections!!!");
                    }
                    else
                    { // 不存在
                        this->uav_ = std::make_shared<UAVBasic>(this->nh_, uav_id, (Communication *)this);
                        sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "UAV" + to_string(ROBOT_ID) + " connection succeeded!!!");
                        // 自启动
                        if (autoload)
                            system(OPENUAVBASIC.c_str());
                    }
                    break; // 退出循环
                }
                else
                {
                    sendTextInfo(TextInfo::MessageTypeGrade::MTG_WARN, "Robot" + to_string(key) + " connection failed, The ground station ID is inconsistent with the communication node ID.");
                    return;
                }
            }
        }
        this->is_heartbeat_ready_ = true;
        // 加载单机参数
        sendControlParam();
        // 加载通信模块参数
        sendCommunicationParam();
        // 加载轨迹控制参数
        // sendCommandPubParam();
    }
    
    
    else if (mode_selection.mode == ModeSelection::Mode::AUTONOMOUSLANDING)
    {
    }
    // 目标识别与跟踪模式
    else if (mode_selection.mode == ModeSelection::Mode::OBJECTTRACKING)
    {
    }
    else if (mode_selection.mode == ModeSelection::Mode::CUSTOMMODE)
    {
        std::string prefix = "mavlink_console";

        if (mode_selection.cmd.length() > prefix.length())
        {
            if (mode_selection.cmd.compare(0, prefix.length(), prefix) == 0)
            {
                if (this->uav_)
                {
                    std::string cmd = mode_selection.cmd.substr(prefix.length() + 1, mode_selection.cmd.length());
                    this->uav_->serialControlPub(cmd);
                }
            }
            else
            {
                if (autoload)
                    system(mode_selection.cmd.c_str());
                sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "CMD:" + mode_selection.cmd);
            }
        }
        else
        {
            if (autoload)
                system(mode_selection.cmd.c_str());
            sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "CMD:" + mode_selection.cmd);
        }
    }
    else if (mode_selection.mode == ModeSelection::Mode::EGOPLANNER)
    {
    }
    else if (mode_selection.mode == ModeSelection::Mode::TRAJECTOYCONTROL)
    {
    }

    this->current_mode_ = mode_selection.mode;
}

void CommunicationBridge::deleteMode(struct ModeSelection mode_selection)
{
    struct TextInfo text_info;
    text_info.MessageType = TextInfo::MessageTypeGrade::MTG_INFO;
    text_info.sec = ros::Time::now().sec;

    if (mode_selection.mode == ModeSelection::Mode::UAVBASIC)
    {
        if (this->is_simulation_ == 1)
        {
            for (int i = 0; i < mode_selection.selectId.size(); i++)
            {
                int key = mode_selection.selectId[i];
                if (this->uavs_.find(key) != this->uavs_.end())
                {
                    // 删除
                    this->uavs_[key].reset();
                    this->uavs_.erase(key);
                    if (ROBOT_ID == key)
                    {
                        this->is_heartbeat_ready_ = false;
                        this->uav_.reset();
                        if (autoload)
                            system(CLOSEUAVBASIC.c_str());
                    }
                }
                sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "Simulation UAV" + to_string(key) + " disconnect!!!");
            }
        }
        else
        {
            if (std::find(mode_selection.selectId.begin(), mode_selection.selectId.end(), ROBOT_ID) != mode_selection.selectId.end()) // 存在本机ID
            {
                if (this->uav_)
                {
                    this->uav_.reset();
                    if (autoload)
                        system(CLOSEUAVBASIC.c_str());
                    sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "UAV" + to_string(ROBOT_ID) + " disconnect!!!");
                }
            }
        }
        this->is_heartbeat_ready_ = false;
    }
    
    else if (mode_selection.mode == ModeSelection::Mode::AUTONOMOUSLANDING)
    {
    }
    else if (mode_selection.mode == ModeSelection::Mode::OBJECTTRACKING)
    {
    }
    else if (mode_selection.mode == ModeSelection::Mode::EGOPLANNER)
    {
    }
}

// 接收组播地址的数据
void CommunicationBridge::multicastUdpFun()
{
    while (this->is_simulation_ == 1)
    {
        for (auto it = this->uavs_.begin(); it != this->uavs_.end(); it++)
        {
            // 开启互斥锁
            std::lock_guard<std::mutex> lg_mutex(g_mutex_);
        }

        usleep(100000); // 控制更新频率 100ms
    }
    int valread;
    if (waitConnectionFromMulticast(UDP_PORT) < 0)
    {
        ROS_ERROR("[bridge_node]Socket recever creation error!");
        exit(EXIT_FAILURE);
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_udp_ip.c_str());

    setsockopt(udp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    sockaddr_in srv_Addr; // 用于存储发送方信息
    socklen_t addr_len = sizeof(srv_Addr);

    while (true)
    {
        valread = recvfrom(udp_socket, udp_recv_buf, BUF_LEN, 0, (struct sockaddr *)&srv_Addr, &addr_len);

        if (valread <= 0)
        {
            ROS_ERROR("Received message length <= 0, maybe connection has lost");
            continue;
        }

        std::lock_guard<std::mutex> lg(g_mutex_);
        std::cout << "udp valread: " << valread << std::endl;
        pubMsg(decodeMsg(udp_recv_buf, Send_Mode::UDP));
    }
}

// 无人机触发安全机制（一般为心跳包丢失） 进行降落
void CommunicationBridge::triggerUAV()
{
    if (this->uav_)
    {
        // 触发降落  暂定
        struct UAVCommand uav_command;
        uav_command.Agent_CMD = UAVCommand::AgentCMD::Land;
        uav_command.Move_mode = UAVCommand::MoveMode::XYZ_VEL;
        uav_command.yaw_ref = 0;
        uav_command.Yaw_Rate_Mode = true;
        uav_command.yaw_rate_ref = 0;
        uav_command.latitude = 0;
        uav_command.longitude = 0;
        uav_command.altitude = 0;
        for (int i = 0; i < 3; i++)
        {
            uav_command.position_ref[i] = 0;
            uav_command.velocity_ref[i] = 0;
            uav_command.acceleration_ref[i] = 0;
            uav_command.att_ref[i] = 0;
        }
        uav_command.att_ref[3] = 0;
        this->uav_->uavCmdPub(uav_command);
    }
}
// 集群触发安全机制（一般为心跳包丢失）



// 给地面站发送心跳包,  超时检测
void CommunicationBridge::toGroundStationFun()
{
    while (true)
    {
        

        std::string message = "CPUUsage:" + to_string(getCPUUsage());
        message += ",CPUTemperature:" + to_string(getCPUTemperature());
        ros::V_string v_nodes;
        ros::master::getNodes(v_nodes);
        for (auto elem : v_nodes)
        {
            message += ",rosnode:" + elem;
        }

        if (!this->is_heartbeat_ready_)
        {
            return;
        }
        heartbeat.message = message;
        // std::cout << disconnect_num << std::endl;
        sendMsgByTcp(encodeMsg(Send_Mode::TCP, heartbeat), udp_ip);
        heartbeat_count++;
        heartbeat.count = heartbeat_count;
        if (disconnect_num >= try_connect_num) // 跟地面站断联后的措施
        {
            disconnect_flag = true;
            std::cout << "conenect ground station failed, please check the ground station IP!" << std::endl;
            // 如果是集群模式 由集群模块触发降落
            if (this->uav_)
            {
                triggerUAV();
                sendTextInfo(TextInfo::MessageTypeGrade::MTG_ERROR, "TCP:" + udp_ip + " abnormal communication,trigger landing.");
            }
            // 无人车  停止小车
            // 触发机制后 心跳准备标志置为false，停止心跳包的发送 再次接收到地面站指令激活
            this->is_heartbeat_ready_ = false;
        }
        else if (disconnect_flag)
        {
            disconnect_flag = false;
            sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "TCP:" + udp_ip + " communication returns to normal.");
        }

        // 无人机数据或者无人车数据是否超时
        if (this->uav_ )
        {
            uint time_stamp = 0;
            std::string type = "UAV";
            if (this->uav_)
            {
                time_stamp = this->uav_->getTimeStamp();
            }
            // 拿单机状态时间戳进行比较 如果不相等说明数据在更新
            static bool flag = true;
            if (time != time_stamp)
            {
                time = time_stamp;
                time_count = 0;
            }
            else // 相等 数据未更新
            {
                time_count++;
            }
        }

        sleep(1);
    }
}

void CommunicationBridge::toGroundHeartbeat(const ros::TimerEvent &time_event)
{
    std::string message = "CPUUsage:" + to_string(getCPUUsage());
    message += ",CPUTemperature:" + to_string(getCPUTemperature());
    ros::V_string v_nodes;
    ros::master::getNodes(v_nodes);
    for (auto elem : v_nodes)
    {
        message += ",rosnode:" + elem;
    }

    if (!this->is_heartbeat_ready_)
    {
        return;
    }
    heartbeat.message = message;
    // std::cout << "disconnect_num:" << disconnect_num << std::endl;
    sendMsgByTcp(encodeMsg(Send_Mode::TCP, heartbeat), udp_ip);
    heartbeat_count++;
    heartbeat.count = heartbeat_count;
    if (disconnect_num >= try_connect_num) // 跟地面站断联后的措施
    {
        disconnect_flag = true;
        std::cout << "conenect ground station failed, please check the ground station IP!" << std::endl;
        // 开启互斥锁
        std::lock_guard<std::mutex> lg_mutex(g_mutex_);
        if (this->uav_)
        {
            triggerUAV();
            sendTextInfo(TextInfo::MessageTypeGrade::MTG_ERROR, "TCP:" + udp_ip + " abnormal communication,trigger landing.");
        }
        // 无人车  停止小车
        // 触发机制后 心跳准备标志置为false，停止心跳包的发送 再次接收到地面站指令激活
        this->is_heartbeat_ready_ = false;
    }
    else if (disconnect_flag)
    {
        disconnect_flag = false;
        // 开启互斥锁
        std::lock_guard<std::mutex> lg_mutex(g_mutex_);
        sendTextInfo(TextInfo::MessageTypeGrade::MTG_INFO, "TCP:" + udp_ip + " communication returns to normal.");
    }

    // 开启互斥锁
    std::lock_guard<std::mutex> lg_mutex(g_mutex_);
    // 无人机数据或者无人车数据是否超时
    if (this->uav_ )
    {
        uint time_stamp = 0;
        std::string type = "UAV";
        if (this->uav_)
        {
            time_stamp = this->uav_->getTimeStamp();
        }
        // 拿单机状态时间戳进行比较 如果不相等说明数据在更新
        static bool flag = true;
        if (time != time_stamp)
        {
            time = time_stamp;
            time_count = 0;
        }
        else // 相等 数据未更新
        {
            time_count++;
        }
    }
}

// 心跳包状态检测
void CommunicationBridge::checkHeartbeatState(const ros::TimerEvent &time_event)
{
    static int disconnect = 0;
    if (!disconnect_flag && this->is_heartbeat_ready_)
    {
        static long count = 0;
        if (count != this->heartbeat_count)
        {
            count = heartbeat_count;
            disconnect = 0;
        }
        else
        {
            disconnect++;
            if (disconnect > try_connect_num)
            {
                std::cout << "conenect ground station failed, please check the ground station IP!" << std::endl;
                // 开启互斥锁
                std::lock_guard<std::mutex> lg_mutex(g_mutex_);
                // 如果是集群模式 由集群模块触发降落
                // 无人机 触发降落或者返航
                if (this->uav_)
                {
                    triggerUAV();
                }
                // 无人车  停止小车
                disconnect_flag = true;
                // this->is_heartbeat_ready_ = false;
            }
        }
    }
}

bool CommunicationBridge::getParam(struct Param *param)
{
    if (param->type == Param::Type::INT || param->type == Param::Type::LONG)
    {
        int value = 0;
        if (!nh_.getParam(param->param_name, value))
        {
            return false;
        }
        param->param_value = std::to_string(value);
    }
    else if (param->type == Param::Type::FLOAT)
    {
        float value = 0.0;
        if (!nh_.getParam(param->param_name, value))
        {
            return false;
        }
        param->param_value = std::to_string(value);
    }
    else if (param->type == Param::Type::DOUBLE)
    {
        double value = 0.0;
        if (!nh_.getParam(param->param_name, value))
        {
            return false;
        }
        param->param_value = std::to_string(value);
    }
    else if (param->type == Param::Type::BOOLEAN)
    {
        bool value = false;
        if (!nh_.getParam(param->param_name, value))
        {
            return false;
        }
        if (value)
            param->param_value = "true";
        else
            param->param_value = "false";
    }
    else if (param->type == Param::Type::STRING)
    {
        std::string value = "";
        if (!nh_.getParam(param->param_name, value))
        {
            return false;
        }
        param->param_value = value;
    }
    return true;
}

void CommunicationBridge::sendControlParam()
{
    /// communication_bridge/control/
    std::string param_name[15] = {"pos_controller", "enable_external_control", "Takeoff_height", "Land_speed", "Disarm_height", "location_source", "maximum_safe_vel_xy", "maximum_safe_vel_z", "maximum_vel_error_for_vision", "x_min", "x_max", "y_min", "y_max", "z_min", "z_max"};
    int8_t param_type[15] = {Param::Type::INT, Param::Type::BOOLEAN, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::INT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT};
    // sendTextInfo(TextInfo::MTG_INFO, "start loading parameters...");
    // usleep(1000);
    struct ParamSettings param_settings;
    for (int i = 0; i < 15; i++)
    {
        if (i < 9)
            param_name[i] = "/uav_control_main_" + std::to_string(uav_id) + "/control/" + param_name[i];
        else
            param_name[i] = "/uav_control_main_" + std::to_string(uav_id) + "/geo_fence/" + param_name[i];
        struct Param param;
        param.param_name = param_name[i];
        param.type = param_type[i];
        if (getParam(&param))
        {
            param_settings.params.push_back(param);
        }
        else
        {
            sendTextInfo(TextInfo::MTG_INFO, "uav control node parameter loading failed...");
            return;
        }
    }
    param_settings.param_module = ParamSettings::ParamModule::UAVCONTROL;
    sendMsgByUdp(encodeMsg(Send_Mode::UDP, param_settings), udp_ip);
    usleep(1000);
    sendTextInfo(TextInfo::MTG_INFO, "uav control node parameter loading success...");
}
void CommunicationBridge::sendCommunicationParam()
{
    std::string param_name[8] = {"ROBOT_ID", "multicast_udp_ip", "ground_station_ip",  "autoload", "uav_control_start", "close_uav_control",  "is_simulation",  "trajectory_ground_control"};
    int8_t param_type[8] = {Param::Type::INT, Param::Type::STRING, Param::Type::STRING, Param::Type::BOOLEAN, Param::Type::STRING, Param::Type::STRING, Param::Type::INT,  Param::Type::BOOLEAN};
    // sendTextInfo(TextInfo::MTG_INFO, "start loading parameters...");
    // usleep(1000);
    struct ParamSettings param_settings;
    for (int i = 0; i < 8 ; i++)
    {
        param_name[i] = "/communication_bridge/" + param_name[i];
        struct Param param;
        param.param_name = param_name[i];
        param.type = param_type[i];
        if (getParam(&param))
        {
            param_settings.params.push_back(param);
        }
        else
        {
            sendTextInfo(TextInfo::MTG_INFO, "communication node parameter loading failed...");
            return;
        }
    }
    param_settings.param_module = ParamSettings::ParamModule::UAVCOMMUNICATION;
    sendMsgByUdp(encodeMsg(Send_Mode::UDP, param_settings), udp_ip);
    usleep(1000);
    sendTextInfo(TextInfo::MTG_INFO, "communication node parameter loading success...");
}

void CommunicationBridge::sendCommandPubParam()
{
    std::string param_name[13] = {"Circle/Center_x", "Circle/Center_y", "Circle/Center_z", "Circle/circle_radius", "Circle/direction", "Circle/linear_vel", "Eight/Center_x", "Eight/Center_y", "Eight/Center_z", "Eight/omega", "Eight/radial", "Step/step_interval", "Step/step_length"};
    int8_t param_type[13] = {Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT, Param::Type::FLOAT};
    // sendTextInfo(TextInfo::MTG_INFO, "start loading parameters...");
    // usleep(1000);
    struct ParamSettings param_settings;
    for (int i = 0; i < 13; i++)
    {
        param_name[i] = "/uav_command_pub/Controller_Test/" + param_name[i];
        struct Param param;
        param.param_name = param_name[i];
        param.type = param_type[i];
        if (getParam(&param))
        {
            param_settings.params.push_back(param);
        }
        else
        {
            sendTextInfo(TextInfo::MTG_INFO, "uav control traject node parameter loading failed...");
            return;
        }
    }
    param_settings.param_module = ParamSettings::ParamModule::UAVCOMMANDPUB;
    sendMsgByUdp(encodeMsg(Send_Mode::UDP, param_settings), udp_ip);
    usleep(1000);
    sendTextInfo(TextInfo::MTG_INFO, "uav control traject node parameter loading success...");
}

void CommunicationBridge::sendTextInfo(uint8_t message_type, std::string message)
{
    struct TextInfo text_info;
    text_info.MessageType = message_type;
    text_info.Message = message;
    text_info.sec = ros::Time::now().sec;
    sendMsgByUdp(encodeMsg(Send_Mode::UDP, text_info), udp_ip);
}

double CommunicationBridge::getCPUUsage()
{
    static CPU_OCCUPY old_cpu_occupy;

    FILE *fd;
    char buff[256];
    CPU_OCCUPY cpu_occupy;
    double cpu_usage = 0.0;

    fd = fopen("/proc/stat", "r");

    if (fd != NULL)
    {
        fgets(buff, sizeof(buff), fd);
        if (strstr(buff, "cpu") != NULL)
        {
            sscanf(buff, "%s %u %u %u %u %u %u %u",
                   cpu_occupy.name, &cpu_occupy.user, &cpu_occupy.nice, &cpu_occupy.system,
                   &cpu_occupy.idle, &cpu_occupy.lowait, &cpu_occupy.irq, &cpu_occupy.softirq);

            double total = (cpu_occupy.user + cpu_occupy.nice + cpu_occupy.system +
                            cpu_occupy.idle + cpu_occupy.lowait + cpu_occupy.irq +
                            cpu_occupy.softirq) -
                           (old_cpu_occupy.user + old_cpu_occupy.nice + old_cpu_occupy.system +
                            old_cpu_occupy.idle + old_cpu_occupy.lowait + old_cpu_occupy.irq +
                            old_cpu_occupy.softirq);

            double idle = cpu_occupy.idle - old_cpu_occupy.idle;

            cpu_usage = (total - idle) / total * 100;

            old_cpu_occupy = cpu_occupy;
        }
    }

    fclose(fd);
    return cpu_usage;
}

double CommunicationBridge::getCPUTemperature()
{
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open())
    {
        std::cerr << "无法打开温度文件！" << std::endl;
        return -1;
    }
    int temperature;
    file >> temperature;
    file.close();

    return temperature / 1000.0; // 温度值通常以千分之一摄氏度为单位
}

void CommunicationBridge::switchLocationSource(int location_source)
{
    ParamManager p(nh_);
    std::string param_name = "/communication_bridge/load_location_source_script/";
    switch (location_source)
    {
    case UAVState::LocationSource::MOCAP:
        param_name += "MOCAP";
        break;
    case UAVState::LocationSource::T265:
        param_name += "T265";
        break;
    case UAVState::LocationSource::GAZEBO:
        param_name += "GAZEBO";
        break;
    case UAVState::LocationSource::FAKE_ODOM:
        param_name += "FAKE_ODOM";
        break;
    case UAVState::LocationSource::GPS:
        param_name += "GPS";
        break;
    case UAVState::LocationSource::RTK:
        param_name += "RTK";
        break;
    case UAVState::LocationSource::UWB:
        param_name += "UWB";
        break;
    case UAVState::LocationSource::VINS:
        param_name += "VINS";
        break;
    case UAVState::LocationSource::OPTICAL_FLOW:
        param_name += "OPTICAL_FLOW";
        break;
    case UAVState::LocationSource::VIOBOT:
        param_name += "VIOBOT";
        break;
    case UAVState::LocationSource::MID360:
        param_name += "MID360";
        break;
    case UAVState::LocationSource::BSA_SLAM:
        param_name += "BSA_SLAM";
        break;
    default:
        param_name += "UNKNOW";
        break;
    }
    std::unordered_map<std::string, std::string> param_map = p.getParams(param_name);
    if(param_map.size() == 1){
        std::string cmd = param_map.begin()->second;
        if(autoload) system(cmd.c_str());
    }
}