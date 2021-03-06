/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** main.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** H264 capture using V4L2                                                            
** RTSP using live555                                                                 
**                                                                                    
** NOTE : Configuration SPS/PPS need to be captured in one single frame               
** -------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <sstream>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <Base64.hh>

// project
#include "ServerMediaSubsession.h"

// -----------------------------------------
//    signal handler
// -----------------------------------------
char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}

// -----------------------------------------
//    signal handler
// -----------------------------------------
void addSession(RTSPServer* rtspServer, const char* sessionName, ServerMediaSubsession *subSession)
{
	UsageEnvironment& env(rtspServer->envir());
	ServerMediaSession* sms = ServerMediaSession::createNew(env, sessionName);
	sms->addSubsession(subSession);
	rtspServer->addServerMediaSession(sms);

	char* url = rtspServer->rtspURL(sms);
	env << "Play this stream using the URL \"" << url << "\"\n";
	delete[] url;			
}

// -----------------------------------------
//    entry point
// -----------------------------------------
int main(int argc, char** argv) 
{
	// default parameters
	const char *dev_name = "/dev/video0";	
	int format = V4L2_PIX_FMT_H264;
	int width = 640;
	int height = 480;
	int queueSize = 10;
	int fps = 25;
	unsigned short rtpPortNum = 20000;
	unsigned short rtcpPortNum = rtpPortNum+1;
	unsigned char ttl = 5;
	struct in_addr destinationAddress;
	unsigned short rtspPort = 8554;
	unsigned short rtspOverHTTPPort = 8080;
	bool multicast = false;
	bool verbose = false;
	std::string outputFile;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:P:F:vO:mT:")) != -1)
	{
		switch (c)
		{
			case 'O':	outputFile = optarg; break;
			case 'v':	verbose = true; break;
			case 'm':	multicast = true; break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'P':	rtspPort = atoi(optarg); break;
			case 'T':	rtspOverHTTPPort = atoi(optarg); break;
			case 'F':	fps = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-v][-m][-P RTSP port][-P RTSP/HTTP port][-Q queueSize] [-W width] [-H height] [-F fps] [-O file] [device]" << std::endl;
				std::cout << "\t -v       : Verbose " << std::endl;
				std::cout << "\t -Q length: Number of frame queue  (default "<< queueSize << ")" << std::endl;
				std::cout << "\t -O file  : Dump capture to a file" << std::endl;
				std::cout << "\t RTSP options :" << std::endl;
				std::cout << "\t -m       : Enable multicast output" << std::endl;
				std::cout << "\t -P port  : RTSP port (default "<< rtspPort << ")" << std::endl;
				std::cout << "\t -H port  : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")" << std::endl;
				std::cout << "\t V4L2 options :" << std::endl;
				std::cout << "\t -F fps   : V4L2 capture framerate (default "<< fps << ")" << std::endl;
				std::cout << "\t -W width : V4L2 capture width (default "<< width << ")" << std::endl;
				std::cout << "\t -H height: V4L2 capture height (default "<< height << ")" << std::endl;
				std::cout << "\t device   : V4L2 capture device (default "<< dev_name << ")" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
     
	// 
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	
	
	RTSPServer* rtspServer = RTSPServer::createNew(*env, rtspPort);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	}
	else
	{
		// set http tunneling
		rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
		
		// Init capture
		*env << "Create V4L2 Source..." << dev_name << "\n";
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name,format,queueSize,width,height,fps,verbose,outputFile);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source for device " << dev_name << "\n";
		}
		else
		{
			destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);	
			OutPacketBuffer::maxSize = videoES->getBufferSize();
			StreamReplicator* replicator = StreamReplicator::createNew(*env, videoES, false);

			// Create Server Multicast Session
			if (multicast)
			{
				addSession(rtspServer, "multicast", MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, 96, replicator,format));
			}
			
			// Create Server Unicast Session
			addSession(rtspServer, "unicast", UnicastServerMediaSubsession::createNew(*env,replicator,format));

			// main loop
			signal(SIGINT,sighandler);
			env->taskScheduler().doEventLoop(&quit); 
			*env << "Exiting..\n";			
		}
		
		Medium::close(videoES);
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;
	
	
	return 0;
}

