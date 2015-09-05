//
//  SocketManager.cpp
//  cocos2d_libs
//
//  Created by li on 15/8/31.
//
//

#if (CC_TARGET_PLATFORM == CC_PLATFORM_MAC || CC_TARGET_PLATFORM == CC_PLATFORM_IOS || CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#endif

#include "SocketManager.h"

static const unsigned int SOCKET_BUFF_SIZE = (10*1024);
static const unsigned int MSG_HEADER_SIZE = sizeof(MessageHeader);

static SocketManager *g_SocketManager = nullptr;

SocketManager* SocketManager::getInstance()
{
    if (nullptr == g_SocketManager)
    {
        g_SocketManager = new SocketManager();
    }
    
    return g_SocketManager;
}


SocketManager::SocketManager()
{
    _socketfd = -1;
}


SocketManager::~SocketManager()
{
}

void SocketManager::parseBuff(char *dataBuff, long dataBuffSize, long *offset)
{
    if (dataBuffSize <= 0) return;
    
    if (dataBuffSize >= MSG_HEADER_SIZE)
    {
        MessageHeader *header = (MessageHeader*)dataBuff;
        unsigned int messageSize = (NTOHL(header->size));
        if (dataBuffSize >= MSG_HEADER_SIZE + messageSize)
        {
            this->recvData(dataBuff, MSG_HEADER_SIZE + messageSize);
            *offset = *offset + messageSize + MSG_HEADER_SIZE;
            this->parseBuff(dataBuff + MSG_HEADER_SIZE + messageSize, dataBuffSize - MSG_HEADER_SIZE - messageSize, offset);
        }
    }
}

// network thread: send message and receive message
void SocketManager::startNetworkThread()
{
    _networkThread = std::thread([this]()
    {
        char dataBuff[SOCKET_BUFF_SIZE+50];
        memset(dataBuff, 0, sizeof(dataBuff));
        long dataUnusedSize = 0;
    
        while (true)
        {
            // 1. process send message
            Message *msg = this->getSendQueueMessage();
            if (NULL != msg)
            {
                if (send(_socketfd, msg->data, msg->size, 0) <= 0)
                {
                    printf("[ERR]send fail");
                }
            
                MessagePool::getInstance()->freeMessage(msg);
            }
    
            // 2. process recv message
            long recvSize = recv(_socketfd, dataBuff + dataUnusedSize, SOCKET_BUFF_SIZE - dataUnusedSize, 0);
            long offset = 0;
            long dataBuffSize = dataUnusedSize + recvSize;
            this->parseBuff(dataBuff, dataBuffSize, &offset);
            if (offset > 0 && offset < dataBuffSize)
            {
                memcpy(dataBuff, dataBuff + offset, dataBuffSize - offset);
                dataUnusedSize = dataBuffSize - offset;
            }
        }
    });
}


bool SocketManager::connectServer(const std::string ip, unsigned short port)
{
    _ip = ip;
    _port = port;
    
    sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if ((_socketfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("create socket error: %s(errno: %d)\n", strerror(errno),errno);
        return false;
    }
    
    if (connect(_socketfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        printf("connect error: %s(errno: %d)\n",strerror(errno),errno);
        return false;
    }
    
    int flags = fcntl(_socketfd, F_GETFL, 0);
    if (flags < 0 )
    {
        perror("fcntl F_GETFL");
        return false;
    }
    flags |= O_NONBLOCK;
    if (fcntl(_socketfd, F_SETFL, flags) < 0)
    {
        perror("fcntl F_SETFL");
        return false;
    }
    
    // set socket buffer size
    int send_buff_size = SOCKET_BUFF_SIZE;
    if (setsockopt(_socketfd, SOL_SOCKET, SO_SNDBUF, &send_buff_size, sizeof(send_buff_size)) < 0)
    {
        printf("[ERR]setsockopt for sending fail");
    }
    int recv_buff_size = SOCKET_BUFF_SIZE;
    if (setsockopt(_socketfd, SOL_SOCKET, SO_RCVBUF, &recv_buff_size, sizeof(recv_buff_size)) < 0)
    {
        printf("[ERR]setsockopt for receiving fail");
    }
    
    return true;
}


void SocketManager::disConnect()
{
}


int SocketManager::sendData(char *data, int size)
{
    Message *msg = MessagePool::getInstance()->getMessage();
    if (NULL == msg)
    {
        printf("message buffer is empty: %s(errno: %d)\n",strerror(errno),errno);
        return -1;
    }
    
    msg->data = data;
    msg->size = size;
    
    _sendQueueMutex.lock();
    _sendQueue.push_back(msg);
    _sendQueueMutex.unlock();
    
    return 0;
}


Message* SocketManager::getSendQueueMessage()
{
    Message *msg = NULL;
    _sendQueueMutex.lock();
    if (_sendQueue.size() > 0)
    {
        msg = _sendQueue.back();
        _sendQueue.pop_back();
    }
    _sendQueueMutex.unlock();
    return msg;
}


Message* SocketManager::getRecvQueueMessage()
{
    Message *msg = NULL;
    _recvQueueMutex.lock();
    msg = _recvQueue.back();
    _recvQueue.pop_back();
    _recvQueueMutex.unlock();
    return msg;
}


int SocketManager::recvData(char *data, unsigned int size)
{
    Message *msg = MessagePool::getInstance()->getMessage();
    if (NULL == msg)
    {
        printf("message buffer is empty: %s(errno: %d)\n",strerror(errno),errno);
        return -1;
    }
    
    char *msgData = (char *)malloc(size + 10);
    memcpy(msgData, data, size);
    
    msg->data = msgData;
    msg->size = size;
    
    _recvQueueMutex.lock();
    _recvQueue.push_back(msg);
    _recvQueueMutex.unlock();
    
    return 0;
}

//
//Message* SocketManager::getRecvQueueMessage()
//{
//    Message *msg = NULL;
//    _recvQueueMutex.lock();
//    if (_recvQueue.size() > 0)
//    {
//        msg = _recvQueue.back();
//        _recvQueue.pop_back();
//    }
//    _recvQueueMutex.unlock();
//    return msg;
//}


bool SocketManager::setNonBlocking(bool nonBlock)
{
}


bool SocketManager::setTimeOut(int recvTimeoutMs, int sendTimeoutMs)
{
}


bool SocketManager::isConnected()
{
}


// This function is called by game thread
void SocketManager::tick(float dt)
{
    Message *msg = getRecvQueueMessage();
    
    if (NULL != msg)
    {
        MessageHeader *msgHeader = (MessageHeader*)msg->data;
        char *protoData = msg->data + MSG_HEADER_SIZE;
        unsigned int protoDataSize = msg->size - MSG_HEADER_SIZE;
        if (msgHeader->messageID == 1)
        {
        }
        MessagePool::getInstance()->freeMessage(msg);
    }
}


/***************************************************** MessagePool *****************************************************/
#define MAX_MSGPOOL_SIZE    2000

static MessagePool *g_MessagePool = nullptr;

MessagePool* MessagePool::getInstance()
{
    if (nullptr == g_MessagePool)
    {
        g_MessagePool = new MessagePool();
    }
    return g_MessagePool;
}


MessagePool::MessagePool()
{
    for (int i = 0; i < MAX_MSGPOOL_SIZE+10; i++)
    {
        Message *msg = new Message();
        msg->data = NULL;
        msg->size = 0;
        _messagePool.push_back(msg);
    }
}


MessagePool::~MessagePool()
{
}


Message* MessagePool::getMessage()
{
    Message *msg = NULL;
    
    _mutex.lock();
    if (_messagePool.size() > 0)
    {
        msg = _messagePool.back();
        _messagePool.pop_back();
    }
    _mutex.unlock();
    
    return msg;
}


void MessagePool::freeMessage(Message *msg)
{
    free(msg->data);
    msg->data = NULL;
    msg->size = 0;
    
    _mutex.lock();
    _messagePool.push_back(msg);
    _mutex.unlock();
}


