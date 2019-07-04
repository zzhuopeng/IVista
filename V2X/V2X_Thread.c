/**
  ******************************************************************************
  * @file    		HMI_Thread.c
  * @author  		Bruceou
  * @version 		V2.0
  * @date    		2019.02.26
  * @brief  		HMI 线程
  ******************************************************************************
  */
#include "V2X_Thread.h"

/**本地宏/本地常量**/
// 调试打印宏
#define dbg_printf(f, a...)                                       \
    do {                                                          \
        { fprintf(stderr, "%s(%d): " f, __func__, __LINE__, ## a); } \
    } while (0)                                                   \

/**全局变量**/
pthread_t ThreadID_UDPListen;	        //线程号
time_t now;							    //心跳机制(全局时间变量)
time_t GetHeartBeatTime;
pthread_mutex_t UDP_mutex = PTHREAD_MUTEX_INITIALIZER;	//UDP互斥锁
pthread_cond_t UDP_cond = PTHREAD_COND_INITIALIZER;		//UDP条件变量
tSPAT* spat;                            //交通灯消息[全局变量方便调用]


/**函数声明**/
static void V2X_Close ( tV2X* pV2X );
static void* UDP_Lisening_thread ( void* HeartBeatFD );
static int UDP_Client_Connect ( tV2X* pV2X );
static void activate_nonblock ( int fd );


/**
  * @brief     HMI线程函数
  * @param     arg
  * @return    void
  */
extern void* V2X_Thread ( void* arg )
{
	int ret = -ENOSYS;
	dbg_printf ( "V2X Thread start\n" );
	pthread_detach ( pthread_self() );

	//V2X模块参数
	tV2X* pV2X  = calloc ( 1, sizeof ( tV2X ) );
	if ( NULL == pV2X )
	{
		dbg_printf ( "[V2X_Thread]:calloc for pV2X failed\n" );
		goto Error;
	}
	//UDP服务器的IP和端口
	pV2X->ASI.pSIP = "192.168.1.237";//192.168.1.144//192.168.1.130
	pV2X->ASI.SPort = 8080;
	//V2X模块线程周期(微妙us)
	pV2X->Params.Interval.V2X = 10*1000;
	pV2X->Params.Interval.UDP_Listening = 500*1000;

	//交通灯消息
	spat = calloc ( 1, sizeof ( tSPAT ) );
	if ( NULL == spat )
	{
		dbg_printf ( "[V2X_Thread]:calloc for spat failed\n" );
		goto Error;
	}

	while ( 1 )
	{
		// 1.0 依次为各个模块请求建立socket连接
		if ( 0 == pV2X->ASI.clientfd.OBUFD )
		{
			//UDP Socket连接
			pV2X->ASI.clientfd.OBUFD = UDP_Client_Connect ( pV2X );
			dbg_printf ( "[HMI_Thread] start heartbeat lisening!!\n" );
			ret = pthread_create ( &ThreadID_UDPListen, NULL, UDP_Lisening_thread, ( void* ) pV2X );
			if ( 0 != ret )
			{
				dbg_printf ( "[HMI_Thread]:pthread_create() failed\n" );
				goto Error;
			}
		}

		// 2.0 等待信号关闭socketfd，当心跳机制检测到与HMI服务器断开连接，会发送该信号
		pthread_mutex_lock ( &UDP_mutex );
		pthread_cond_wait ( &UDP_cond,&UDP_mutex );
		pthread_mutex_unlock ( &UDP_mutex );
		//心跳线程发送关闭信号后，会自动退出，在这里回收线程资源
		pthread_join ( ThreadID_UDPListen, NULL );

		//3.0 关闭各模块Socket连接
		if ( pV2X->ASI.clientfd.OBUFD )
		{
			close ( pV2X->ASI.clientfd.OBUFD );
			pV2X->ASI.clientfd.OBUFD = 0;
			dbg_printf ( "[V2X_Thread]:UDP Socket Closed\n" );
		}
		//延时10ms周期 重连
		UtilNap ( pV2X->Params.Interval.V2X );
	}

Error:
	dbg_printf ( "[HMI_Thread]:Error!\n" );
	V2X_Close ( pV2X );
	//释放交通灯spat
	free ( spat );
	pthread_exit ( NULL );
}

/**
  * @brief     关闭本地HMI模块
  * @param     void
  * @return    void
  */
static void V2X_Close ( tV2X* pV2X )
{
	//关闭Socket文件描述符
	if ( pV2X->ASI.clientfd.OBUFD )
	{
		close ( pV2X->ASI.clientfd.OBUFD );
		pV2X->ASI.clientfd.OBUFD = 0;
		dbg_printf ( "[V2X_Thread]:UDP Socket Closed\n" );
	}

	// 销毁TCP互斥锁和条件变量
	pthread_mutex_destroy ( &UDP_mutex );
	pthread_cond_destroy ( &UDP_cond );
	//释放HMI模块配置信息
	free ( pV2X );
}


/**
  * @brief     心跳线程，用于检测和HMI服务器的TCP socket连接状态，定时向HMI发送和接收HMI心跳包，
  *	       	   如果定时时间内没接收到HMI心跳包，将关闭所有和HMI建立的socket套接口并等待重连
			   (利用select机制实现的心跳线程，非阻塞IO)
  * @param     心跳Socket句柄
  * @return    void
  */
static void* UDP_Lisening_thread ( void* pV )
{
	tV2X* pV2X = ( tV2X* ) pV;
	char recv_buf[BUFF_MAX_SIZE] = { 0 };

	int clientFd = pV2X->ASI.clientfd.OBUFD;
	activate_nonblock ( clientFd ); //设置为非阻塞

	struct sockaddr* serveraddr = ( struct sockaddr* ) & ( pV2X->ASI.serverAddr );
	socklen_t len = sizeof ( pV2X->ASI.serverAddr );

	// 初始化系统当前时间
	time ( &now );
	time ( &GetHeartBeatTime );

	// 初始化select fd集合
	fd_set fds;
	FD_ZERO ( &fds );

	while ( 1 )
	{
		// 0.0 重新设置超时时间和文件描述符集合
		FD_SET ( clientFd, &fds );
		struct timeval timeout;
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;

		// 1.0 更新系统当前时间
		time ( &now );

		// 2.0 当前系统时间与上一次获取到HMI心跳包的系统时间做差，若大于4s，
		// 认为已经和HMI socket服务器断开连接,退出心跳线程，关闭客户端各个模块socketfd
		if ( now - GetHeartBeatTime > 4  &&  ( -1 == sendto ( clientFd, 1, 1, 0, serveraddr, len ) ) )
		{
			dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] heartbeat time out!!~~~~~~~~~~~~~~~\n" );
			pthread_mutex_lock ( &UDP_mutex );
			pthread_cond_signal ( &UDP_cond );
			pthread_mutex_unlock ( &UDP_mutex );
			break;
		}

		// 3.0 监听心跳socket套接口的可读事件，即监听是否收到V2X服务器的数据
		int ret = select ( clientFd+1, &fds, NULL, NULL, &timeout );
		if ( ret < 0 )
		{
			perror ( "hearbeat select:" );
			break;
		}
		else if ( ret == 0 )
		{
			dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] select timeout!~~~~~~~~~~~~\n" );
			continue;
		}

		// 4.0 select成功检测到监听集合产生可读事件，解析事件是否为服务器发过来的数据
		if ( !FD_ISSET ( clientFd, &fds ) )
		{
			dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] other event!\n" );
			continue;
		}

		// 5.0 接收V2X数据
		memset ( recv_buf, 0, sizeof ( recv_buf ) );
		ret = recvfrom ( clientFd, recv_buf, sizeof ( recv_buf ), 0, ( struct sockaddr* ) serveraddr, &len );
		if ( ret < 0 )
		{
			perror ( "heartbeat recv:" );
			break;
		}
		else if ( ret == 0 )
		{
			dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] peer socket closed!~~~~~~~~~~~~~\n" );
			pthread_mutex_lock ( &UDP_mutex );
			pthread_cond_signal ( &UDP_cond );
			pthread_mutex_unlock ( &UDP_mutex );
			dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] clientFd = %d\n", clientFd );
			break;
		}
		else
		{
			// 6.0 成功接收到心跳包, 更新系统时间
			time ( &GetHeartBeatTime );

			// 7.0 解析接收到的数据
			cJSON* root = cJSON_Parse ( recv_buf );
			if ( NULL == root )
			{
				dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread]cJSON ROOT ERROR\n" );
				continue;
			}
			cJSON* data = cJSON_GetObjectItem ( root, "data" );
			cJSON* item = cJSON_GetObjectItem ( root, "tag" );
			//根据tag的编号，解析对应的数据
			switch ( item->valueint )
			{
				case JSON_MSGID_OBU:
					item = cJSON_GetObjectItem ( data, "device_id" );
					dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread]get OBU Msg:%s\n", item->valuestring );
					break;
				case JSON_MSGID_SPAT:
					item = cJSON_GetObjectItem ( data, "device_id" );
					if ( NULL != item )
					{
						memmove ( spat->device_id, item->valuestring, sizeof ( item->valuestring ) );
					}
					item = cJSON_GetObjectItem ( data, "lat" );
					if ( NULL != item )
					{
						spat->lat = item->valuedouble;
					}
					item = cJSON_GetObjectItem ( data, "lon" );
					if ( NULL != item )
					{
						spat->lon = item->valuedouble;
					}
					item = cJSON_GetObjectItem ( data, "ele" );
					if ( NULL != item )
					{
						spat->ele = ( float ) item->valuedouble;
					}
					item = cJSON_GetObjectItem ( data, "hea" );
					if ( NULL != item )
					{
						spat->hea = ( float ) item->valuedouble;
					}
					//判断获取哪个相位
					item = cJSON_GetObjectItem ( data, "phase" );
					int phaseSize = cJSON_GetArraySize ( item );
					int i=0;
					for ( i=0; i<phaseSize; i++ )
					{
                        cJSON* phaseItem;
						cJSON* object = cJSON_GetArrayItem ( item, i );
                        phaseItem = cJSON_GetObjectItem ( object, "direction" );
						if ( NULL != phaseItem )
						{
							spat->phase.direction = phaseItem->valueint;
						}
                        //direction判断
						phaseItem = cJSON_GetObjectItem ( object, "color" );
						if ( NULL != phaseItem )
						{
							spat->phase.color = phaseItem->valueint;
						}
						phaseItem = cJSON_GetObjectItem ( object, "guide_speed_max" );
						if ( NULL != phaseItem )
						{
							spat->phase.guide_speed_max = phaseItem->valuedouble;
						}
						phaseItem = cJSON_GetObjectItem ( object, "guide_speed_min" );
						if ( NULL != phaseItem )
						{
							spat->phase.guide_speed_min = phaseItem->valuedouble;
						}
						phaseItem = cJSON_GetObjectItem ( object, "id" );
						if ( NULL != phaseItem )
						{
							spat->phase.id = phaseItem->valueint;
						}
						phaseItem = cJSON_GetObjectItem ( object, "time" );
						if ( NULL != phaseItem )
						{
							spat->phase.time = phaseItem->valueint;
						}
					}
					dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] spat lat:%lf, spat lon:%lf\n", spat->lat, spat->lon );
					break;
				case JSON_MSGID_RSI:
					item = cJSON_GetObjectItem ( data, "type" );
					switch ( item->valueint )
					{
						case JSON_MSGID_RSI_EV:
							dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] Emergency vehicle\n" );
							break;
						case JSON_MSGID_RSI_TP:
							dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] Taxi pedestrian\n" );
							break;
						default:
							dbg_printf ( "[V2X_thread]->[UDP_Lisening_thread] RSI Msg Type ERROR\n" );
							break;
					}
					break;
			}

			cJSON_Delete ( root );


			//打印输出
//			recv_buf[ret] = 0;
//			printf ( "received:" );
//			puts ( recv_buf );
		}
		//延时500ms发送心跳
		UtilNap ( pV2X->Params.Interval.UDP_Listening );
	}

	( void ) pthread_exit ( NULL );
	return NULL;
}

/**
  * @brief    	创建一个UDP客户端
  * @param    	ip 服务器IP, port 服务器端口
  * @return   	成功：创建的UDP客户端对应的fd句柄
				失败
  */
static int UDP_Client_Connect ( tV2X* pV2X )
{
	char* buff = "connect\n";

	// 创建socket套接口
	int clientfd = socket ( AF_INET, SOCK_DGRAM, 0 );
	if ( clientfd == -1 )
	{
		perror ( "udp_socket create failed!\n" );
		exit ( -1 );
	}

	// 初始化服务器地址
	memset ( &pV2X->ASI.serverAddr, 0, sizeof ( pV2X->ASI.serverAddr ) );
	pV2X->ASI.serverAddr.sin_family = AF_INET;
	pV2X->ASI.serverAddr.sin_port = htons ( pV2X->ASI.SPort );
	pV2X->ASI.serverAddr.sin_addr.s_addr = inet_addr ( pV2X->ASI.pSIP );

	//向服务器发出连接请求
	dbg_printf ( "[V2X_thread]->[UDP_Client_Connect] Now require connect to server[ %s ] ...\n", pV2X->ASI.pSIP );
	while ( 1 )
	{
		//发送一些数据，测试是否联通(UDP不能通过发送数据判断联通)
		int ret = sendto ( clientfd, buff, strlen ( buff ), 0, ( struct sockaddr* ) & ( pV2X->ASI.serverAddr ), sizeof ( pV2X->ASI.serverAddr ) );
		if ( ret < 0 )
		{
			perror ( "connect:" );
			dbg_printf ( "[V2X_thread]->[UDP_Client_Connect] Now require reconnect to server[ %s ] ...\n", pV2X->ASI.pSIP );
		}
		else if ( ret == 0 )
		{
			perror ( "connect:" );
			dbg_printf ( "[V2X_thread]->[UDP_Client_Connect] Can not Send Msg to server[ %s ] ...\n", pV2X->ASI.pSIP );
		}
		else
		{
			//发送数据成功，建立连接
			dbg_printf ( "[V2X_thread]->[UDP_Client_Connect] connection established.\n" );
			break;
		}
	}
	return clientfd;
}


/**
  * @brief    	将文件描述符I/O设置为非阻塞模式
  * @param    	fd 要设置的文件描述符
  * @return   	void
  */
static void activate_nonblock ( int fd )
{
	int ret;
	int flags = fcntl ( fd, F_GETFL );
	if ( flags == -1 )
	{
		dbg_printf ( "[V2X_thread]->[activate_nonblock] fcntl error!\n" );
		exit ( EXIT_FAILURE );
	}

	flags |= O_NONBLOCK;
	ret = fcntl ( fd, F_SETFL, flags );
	if ( ret == -1 )
	{
		dbg_printf ( "[V2X_thread]->[activate_nonblock] fcntl error!\n" );
		exit ( EXIT_FAILURE );
	}
}

/**
  * @brief     精确延时us微妙
  * @param     us 延时时间（微妙）
  * @return    void
  */
extern void UtilNap ( uint32_t us )
{
	struct timeval tv;
	tv.tv_sec = us/1000000;
	tv.tv_usec = us%1000000;
	//通过select延时
	select ( 0, NULL, NULL, NULL, &tv );
}
