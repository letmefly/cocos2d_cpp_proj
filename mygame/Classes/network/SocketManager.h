//
//  SocketManager.h
//  cocos2d_libs
//
//  Created by li on 15/8/31.
//
//

#ifndef __cocos2d_libs__SocketManager__
#define __cocos2d_libs__SocketManager__

#include <string.h>

typedef struct __message
{
    char *data;
    unsigned int size;
} Message;


typedef struct __MessageHeader
{
	unsigned short messageID;
    unsigned char  protoType;
	unsigned int size;
} MessageHeader;

class SocketManager
{
public:
    static SocketManager* getInstance();
    
    SocketManager();
    
    ~SocketManager();
    
    bool connectServer(const std::string ip, unsigned short port);
    
    void disConnect();
    
    int sendData(char *data, int size);
    
    int recvData(char *data, unsigned int size);
    
    bool setNonBlocking(bool nonBlock);
    
    bool setTimeOut(int recvTimeoutMs, int sendTimeoutMs);
    
    bool isConnected();
    
    void tick(float dt);
    
    Message* getSendQueueMessage();
    Message* getRecvQueueMessage();
    
//    Message* getRecvQueueMessage();
    
private:
    void startNetworkThread();
    void parseBuff(char *dataBuff, long dataBuffSize, long *offset);
    
    std::thread _networkThread;
    
    std::string _ip;
    unsigned short _port;
    int _socketfd;
    
    std::mutex _sendQueueMutex;
    std::vector<Message*> _sendQueue;
    std::mutex _recvQueueMutex;
    std::vector<Message*> _recvQueue;
};


class MessagePool
{
public:
    static MessagePool* getInstance();
    MessagePool();
    ~MessagePool();
    
    Message* getMessage();
    void freeMessage(Message *msg);
    
private:
    std::vector<Message*> _messagePool;
    std::mutex _mutex;
};

#endif /* defined(__cocos2d_libs__SocketManager__) */
