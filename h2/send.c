#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <resolv.h>
#include <signal.h>
#include <getopt.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
在/usr/include/net ethernet.h里面
struct ether_addr
{
  u_int8_t ether_addr_octet[ETH_ALEN];
} __attribute__ ((__packed__));

struct ether_header
{
  u_int8_t  ether_dhost[ETH_ALEN];	// destination eth addr	
  u_int8_t  ether_shost[ETH_ALEN];	// source ether addr	
  u_int16_t ether_type;		        // packet type ID field
} __attribute__ ((__packed__));
------------------------------------------------------------------
在/usr/include/netinet ip.h里面
struct iphdr
  {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    unsigned int ihl:4;
    unsigned int version:4;
#elif __BYTE_ORDER == __BIG_ENDIAN
    unsigned int version:4;
    unsigned int ihl:4;
#else
# error	"Please fix <bits/endian.h>"
#endif
    u_int8_t tos;
    u_int16_t tot_len;
    u_int16_t id;
    u_int16_t frag_off;
    u_int8_t ttl;
    u_int8_t protocol;
    u_int16_t check;
    u_int32_t saddr;
    u_int32_t daddr;
    //The options start here. 
  };
*/

#define IP(a,b,c,d) ((uint32_t)(((a) & 0xff) << 24) | \
				(((b) & 0xff) << 16) | \
				(((c) & 0xff) << 8)  | \
				((d) & 0xff))


#define BUFSIZE	2048 	//This number must be bigger than 1500. 
static int 	RecvBufSize=BUFSIZE;

#define port_name_length 9
struct PORT
{
	char name[port_name_length];
	int sockfd_send;
	int sockfd_receive;
	uint16_t send_buffer_length;
	uint16_t receive_buffer_length;
	uint8_t receive_buffer[BUFSIZE];
	uint8_t send_buffer[BUFSIZE];
	struct ether_addr addr;
	struct sockaddr Port;
};
typedef struct PORT port_t;
/*****************************************
* 功能描述：物理网卡混杂模式属性操作
*****************************************/
static int Ethernet_SetPromisc(const char *pcIfName,int fd,int iFlags)
{
	int iRet = -1;
	struct ifreq stIfr;
	//获取接口属性标志位
	strcpy(stIfr.ifr_name,pcIfName);
	iRet = ioctl(fd,SIOCGIFFLAGS,&stIfr);
	if(0 > iRet){
		perror("[Error]Get Interface Flags");   
		return -1;
	}
	if(0 == iFlags){
		//取消混杂模式
		stIfr.ifr_flags &= ~IFF_PROMISC;
	}
	if(iFlags>0){
		//设置为混杂模式
		stIfr.ifr_flags |= IFF_PROMISC;
	}
	//设置接口标志
	iRet = ioctl(fd,SIOCSIFFLAGS,&stIfr);
	if(0 > iRet){
		perror("[Error]Set Interface Flags");
		return -1;
	}
	return 0;
}

/*****************************************
* 功能描述：创建原始套接字
*****************************************/
static int Ethernet_InitSocket(char Physical_Port[port_name_length])
{
	int iRet = -1;
	int fd = -1;
	struct ifreq stIf;
	struct sockaddr_ll stLocal = {0};
	//创建SOCKET
	fd = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
	if (0 > fd){
		perror("[Error]Initinate L2 raw socket");
		return -1;
	}
	//网卡混杂模式设置
	Ethernet_SetPromisc(Physical_Port,fd,1);
	//设置SOCKET选项
	iRet = setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&RecvBufSize,sizeof(int));
	if (0 > iRet){
		perror("[Error]Set socket option");
		close(fd);
	}
	
	//获取物理网卡接口索引
	strcpy(stIf.ifr_name,Physical_Port);
	iRet = ioctl(fd,SIOCGIFINDEX,&stIf);
	if (0 > iRet){
		perror("[Error]Ioctl operation");
		close(fd);
		return -1;
	}
	//绑定物理网卡
	stLocal.sll_family = PF_PACKET;
	stLocal.sll_ifindex = stIf.ifr_ifindex;
	stLocal.sll_protocol = htons(ETH_P_ALL);
	iRet = bind(fd,(struct sockaddr *)&stLocal,sizeof(stLocal));
	if (0 > iRet){
		perror("[Error]Bind the interface");
		close(fd);
		return -1;
	}
	return fd;   
}

port_t * create_a_port(const char* name, uint64_t mac)
{
	printf("[From %s]Creating a port,name=%s\n",__func__,name);
	port_t * port=malloc(sizeof(port_t));
//---------------------------------------------------------------------
	memcpy(port->name,name,port_name_length);

	int i=0;
	for(i=0;i<ETH_ALEN;i++)
	{	
		port->addr.ether_addr_octet[ETH_ALEN-i-1]=(uint8_t)(mac&0xFF);
		mac=mac>>8;		
	}
	//memcpy(port->addr.ether_addr_octet,mac,ETH_ALEN);

	port->sockfd_receive=Ethernet_InitSocket(port->name);
	if((port->sockfd_send=socket(PF_PACKET,SOCK_PACKET,htons(ETH_P_ALL)))==-1)
	{
		printf("Socket Error\n");
		exit(0);
	}
	memset(&port->Port,0,sizeof(port->Port));
	strcpy(port->Port.sa_data,port->name);
//---------------------------------------------------------------------	
	return port;
}

/*
htons 把unsigned short类型从主机序转换到网络序(2字节 16位)
htonl 把unsigned long类型从主机序转换到网络序(4字节 32位)
ntohs 把unsigned short类型从网络序转换到主机序
ntohl 把unsigned long类型从网络序转换到主机序
*/

void *thread_send();
void *thread_recv();

int main()
{	
/*	
	uint32_t sum_32=(uint32_t)(0x4500+0x00c8+0xabcd +0x0040+0x4011+0x0a0b+0x0c0d+0x0b0b+0x0b0b);
	uint16_t sum=(uint16_t)((sum_32>>16)+(sum_32&0xffff));
	printf("sum=%x\n",0xffff-sum);
*/
	port_t * port1=create_a_port("eth0",0x000400000001);
	//MAC地址采用8禁止的方式表示～
//--------------------------------------------------------------------------------
	pthread_t pthread_recv;
	if(pthread_create(&pthread_recv, NULL, thread_recv, (void *)port1)!=0)
	{
		perror("Creation of receive thread failed.");
	}
	
	//沉睡一秒钟,等待发包~	
	sleep(1);
	
	pthread_t pthread_send;
	if(pthread_create(&pthread_send, NULL, thread_send, (void *)port1)!=0)
	{
		perror("Creation of send thread failed.");
	}
	

//--------------------------------------------------------------------------------	

	while(1)
	{
		
	}
	return (EXIT_SUCCESS);
	return 0;	
}

void *thread_send(void * p_port)
{		
	printf("[Entering %s]\n",__func__);
	port_t * port=(port_t *)p_port;
	//MAC层	
	struct ether_header * p_ether_header =(struct ether_header *)port->send_buffer;
	memset(&p_ether_header-> ether_dhost,0xFF,ETH_ALEN);
	memcpy(&p_ether_header-> ether_shost,port->addr.ether_addr_octet,ETH_ALEN);
	p_ether_header-> ether_type=0x0008;
	port->send_buffer_length=sizeof(struct ether_header);

	//IPv4层
	struct iphdr * p_iphdr =(struct iphdr *)(p_ether_header+1);

	p_iphdr->version=4;	//IPv4的版本为4,IPv6的版本为6.
	p_iphdr->ihl=5;		//由于没有可选字段,这个值为20字节,这个字段是多少个四个字节,所以是5.
	p_iphdr->tos=0;		//服务类型,分为Precedence和TOS.现在基本不使用了~
	p_iphdr->tot_len=200;	//以太网协议对帧的数据有最大值（1500字节）和最小值（46字节）的限制，当数据小于46字节时，数据将含有填充数据
	p_iphdr->id=0xABCD;	//在到达终点时终点能根据标识号将同一个数据报的分片重新组装成一个数据报。
	p_iphdr->frag_off=0x4000;	//前面3位是Flags(010表示路由器不要分段) 后面13位是偏移量,由于字节序的问题所以是0x0040.
					/*Flags 第一位保留（未用），第二位为“不分片（do not fragment）”，第三位位“还有分片（more fragment）”。*/
	p_iphdr->ttl=64;	//每进过一个路由器 TTL减1,缺省的值为64
	p_iphdr->protocol=17;	//1表示ICMP，2表示IGMP，6表示TCP，17表示UDP，89表示OSPF
	p_iphdr->check=0;	//后面对这个check进行更新!!!
	p_iphdr->saddr=IP(10,0,1,10);
	p_iphdr->daddr=IP(10,0,0,10);

	port->send_buffer_length+=p_iphdr->tot_len;	
	//调节字节序
	p_iphdr->tot_len=	htons(p_iphdr->tot_len);
	p_iphdr->id=		htons(p_iphdr->id);
	p_iphdr->frag_off=	htons(p_iphdr->frag_off);
	p_iphdr->saddr=		htonl(p_iphdr->saddr);
	p_iphdr->daddr=		htonl(p_iphdr->daddr);
	
	//下面的步骤位计算ipv4的checksum部分
	uint16_t * p_16=(uint16_t *)p_iphdr;
	int add_cycle=0;
	uint32_t sum_32_temp=0;
	for(add_cycle=0;add_cycle<10;add_cycle++)
	{
		sum_32_temp+=(*p_16);
		p_16++;
	}
	//p_iphdr->check=0xFFFF-((uint16_t)((sum_32_temp>>16)+(sum_32_temp&0xffff)));
	//调节字节序
	printf("sum=%X\n",p_iphdr->check);
	//这里很奇怪,不用调换网络的字节序号!!!
	//p_iphdr->check=		htons(p_iphdr->check);
	
	sendto(port->sockfd_send,port->send_buffer,port->send_buffer_length,0,&port->Port,sizeof(port->Port));
	
	printf("[Leaving %s]\n",__func__);
	pthread_exit(NULL);
}

void *thread_recv(void * p_port)
{	
	printf("[Entering %s]\n",__func__);
	port_t * port=(port_t *)p_port;
	
	//并不知道这个字段是干嘛的~
	socklen_t RecvSocketLen = 0;
	int RecvLength=0;
	uint8_t broadcast_MAC[ETH_ALEN]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	int counter=0;
	while(1)
	{
		while(RecvLength = recvfrom(port->sockfd_receive, port->receive_buffer, RecvBufSize, 0, NULL, &RecvSocketLen))
		{
			
			struct ether_header * p_ether_header =(struct ether_header *)port->receive_buffer;	
			//调到这里!!
			if(memcmp(port->addr.ether_addr_octet,p_ether_header->ether_shost,ETH_ALEN)!=0 //源地址不是本机
			|| memcmp(port->addr.ether_addr_octet,p_ether_header->ether_dhost,ETH_ALEN)==0 //发往本机的
			|| memcmp(broadcast_MAC,p_ether_header->ether_dhost,ETH_ALEN)==0)	       //广播包
			{
				
				//printf("[From %s]Receiving a MAC packet!\n",__func__);
				struct iphdr * p_iphdr =(struct iphdr *)(p_ether_header+1);
				if(p_iphdr->version==0x04)
				{
					printf("\n________%d_________\n",++counter);
					printf("[From %s]Receiving a IPv4 packet!\n",__func__);
					
					printf("TTL=%u\n",p_iphdr->ttl);
					printf("CHECK=%X\n",p_iphdr->check);

					uint16_t * p_16=(uint16_t *)p_iphdr;
					int add_cycle=0;
					uint32_t sum_32_temp=0;
					for(add_cycle=0;add_cycle<10;add_cycle++)
					{
						//这里要跳过Checksum部分～
						if(add_cycle!=5)
						{
							sum_32_temp+=(*p_16);
						}
						
						p_16++;
					}
					uint16_t now_check=0xFFFF-((uint16_t)((sum_32_temp>>16)+(sum_32_temp&0xffff)));
					printf("now_check=%X\n",now_check);
				}
				
			}
		}
	}

	printf("[Leaving %s]\n",__func__);
	pthread_exit(NULL);
}
