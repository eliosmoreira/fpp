/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the GPL v2 as described in the
 * included LICENSE.GPL file.
 */

#include "fpp-pch.h"

#include "fpp.h"
#include "fppversion.h"
#include "command.h"

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

int socket_fd;
struct sockaddr_un server_address;
struct sockaddr_un client_address;
int bytes_received, bytes_sent;

char command[8192];
char response[256];

void SendCommand(const char * com);
void SetupDomainSocket(void);
void Usage(char *appname);

socklen_t address_length;

int main (int argc, char *argv[])
{
  memset(command, 0, sizeof(command));
  SetupDomainSocket();
  if(argc>1)
  {
    // Status - example "fpp -s"
    if(strncmp(argv[1],"-s",2)==0)
    {
      SendCommand("s");
    }
    // Set Volume - example "fpp -v 50"
    else if(strncmp(argv[1],"-v",2)==0)
    {
      snprintf(command,sizeof(command),"v,%s,",argv[2]);
      SendCommand(command);
    }
    // Display version information
    else if(strncmp(argv[1],"-V",2)==0)
    {
      printVersionInfo();
	  exit(0);
    }
    // Play Playlist - example "fpp -p playlistFile"
    else if((strncmp(argv[1],"-p",2) == 0) &&  argc > 2)
    {
      snprintf(command,sizeof(command),"p,%s,%s,",argv[2],argv[3]);
      SendCommand(command);
    }
    // Play Playlist only once- example "fpp -P playlistFile 1"
    else if((strncmp(argv[1],"-P",2) == 0) &&  argc > 2)
    {
      snprintf(command,sizeof(command),"P,%s,%s,",argv[2],argv[3]);
      SendCommand(command);
    }
    // Stop gracefully - example "fpp -S"
    else if(strncmp(argv[1],"-S",2) == 0)
    {
      sprintf(command,"StopGracefully");
      SendCommand(command);
    }
    // Stop gracefully After Loop - example "fpp -L"
    else if(strncmp(argv[1],"-L",2) == 0)
    {
      sprintf(command,"StopGracefullyAfterLoop");
      SendCommand(command);
    }
    // Stop now - example "fpp -d"
    else if(strncmp(argv[1],"-d",2) == 0)
    {
      sprintf(command,"StopNow");
      SendCommand(command);
    }
    // Shutdown fppd daemon
    else if(strncmp(argv[1],"-q",2) == 0)
    {
      sprintf(command,"q");
      SendCommand(command);
    }
    // Restart fppd daemon
    else if(strncmp(argv[1],"-r",2) == 0)
    {
      sprintf(command,"restart");
      SendCommand(command);
    }
    // Reload schedule example "fpp -R"
    else if(strncmp(argv[1],"-R",2) == 0)
    {
      sprintf(command,"R");
      SendCommand(command);
    }
    else if((strncmp(argv[1],"-c",2) == 0) && argc > 2)
    {
      if (!strcmp(argv[2], "next"))
        strcpy(command,"NextPlaylistItem");
      else if (!strcmp(argv[2], "prev"))
        strcpy(command,"PrevPlaylistItem");
      else if (!strcmp(argv[2], "pause"))
        sprintf(command,"ToggleSequencePause");
      else if (!strcmp(argv[2], "step"))
        sprintf(command,"SingleStepSequence");
      else if (!strcmp(argv[2], "stepback"))
        sprintf(command,"SingleStepSequenceBack");
      else if (!strcmp(argv[2], "stop"))
        sprintf(command,"StopNow");
      else if (!strcmp(argv[2], "graceful"))
        sprintf(command,"StopGracefully");
      SendCommand(command);
    }
    // Start an effect - example "fpp -e effectName"
    else if((strncmp(argv[1],"-e",2) == 0) &&  argc > 2)
    {
      if (strchr(argv[2], ','))
        snprintf(command,sizeof(command),"e,%s,",argv[2]);
      else
        snprintf(command,sizeof(command),"e,%s,1,0,",argv[2]);
      SendCommand(command);
    }
    // Stop an effect - example "fpp -e effectName"
    else if((strncmp(argv[1],"-E",2) == 0) &&  argc > 2)
    {
      if (!strcmp(argv[2], "ALL"))
        strcpy(command,"StopAllEffects,");
      else
        snprintf(command,sizeof(command),"StopEffectByName,%s,",argv[2]);
      SendCommand(command);
    }
    // Trigger a FPP Command Preset Slot - example "fpp -t 12"
    else if((strncmp(argv[1],"-t",2) == 0) &&  argc > 2)
    {
      snprintf(command,sizeof(command),"t,%s,",argv[2]);
      SendCommand(command);
    }
    else if(strncmp(argv[1],"-h",2) == 0)
    {
      Usage(argv[0]);
    }
    // Set new log level - "fpp --log-level info"   "fpp --log-level debug"
    else if((strcmp(argv[1],"--log-level") == 0) &&  argc > 2)
    {
      snprintf(command,sizeof(command),"LogLevel,%s,", argv[2]);
      SendCommand(command);
    }
    // Set new log mask - "fpp --log-mask channel,mediaout"   "fpp --log-mask all"
    else if((strcmp(argv[1],"--log-mask") == 0) &&  argc > 2)
    {
      char newMask[128];
      strncpy(newMask, argv[2], sizeof(newMask));
      newMask[sizeof(newMask)-1] = '\0'; 

      char *s = strchr(argv[2], ',');
      while (s) {
        *s = ';';
        s = strchr(s+1, ',');
      }

      sprintf(command,"LogMask,%s,", newMask);
      SendCommand(command);
    }
    // Configure the given GPIO to the given mode
    else if((strncmp(argv[1],"-G",2) == 0) &&  argc == 3) {
        snprintf(command,sizeof(command),"SetupExtGPIO,%s,",argv[2]);
        SendCommand(command);
    } else if((strncmp(argv[1],"-G",2) == 0) &&  argc == 4) {
        snprintf(command,sizeof(command),"SetupExtGPIO,%s,%s,",argv[2],argv[3]);
        SendCommand(command);
    // Set the given GPIO to the given value
    } else if((strncmp(argv[1],"-g",2) == 0) &&  argc == 3) {
        snprintf(command,sizeof(command),"ExtGPIO,%s",argv[2]);
        SendCommand(command);
    } else if((strncmp(argv[1],"-g",2) == 0) &&  argc == 5) {
        snprintf(command,sizeof(command),"ExtGPIO,%s,%s,%s",argv[2],argv[3],argv[4]);
        SendCommand(command);
    } else if((strncmp(argv[1],"-C",2) == 0)) {
        Json::Value val;
        val["command"] = argv[2];
        for (int x = 3; x < argc; x++) {
            val["args"].append(argv[x]);
        }
        std::string js = SaveJsonToString(val);
        std::string resp;
        urlPost("http://localhost/api/command", js, resp);
        printf("Result: %s\n", resp.c_str());
        return 0;
    } else {
        Usage(argv[0]);
    }
  }
  else
  {
    return 0;
  }

  return 0;
}

/*
 *
 */
void SetupDomainSocket(void)
{
 if((socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
 {
  perror("fpp client socket");
  return;
 }

    mkdir(FPP_SOCKET_PATH, 0777);
    chmod(FPP_SOCKET_PATH, 0777);

 memset(&client_address, 0, sizeof(struct sockaddr_un));
 client_address.sun_family = AF_UNIX;
 strcpy(client_address.sun_path, FPP_CLIENT_SOCKET);

 unlink(FPP_CLIENT_SOCKET);
 if(bind(socket_fd, (const struct sockaddr *) &client_address,
         sizeof(struct sockaddr_un)) < 0)
 {
  perror("fpp client bind");
  return;
 }
 memset(&server_address, 0, sizeof(struct sockaddr_un));
 server_address.sun_family = AF_UNIX;
 strcpy(server_address.sun_path, FPP_SERVER_SOCKET);
    
    symlink(FPP_SERVER_SOCKET, FPP_SERVER_SOCKET_OLD);
    symlink(FPP_CLIENT_SOCKET, FPP_CLIENT_SOCKET_OLD);
}

/*
 *
 */
void SendCommand(const char * com)
{
 int max_timeout = 4000;
 int i=0;
 bytes_sent = sendto(socket_fd, com, strlen(com), 0,
                     (struct sockaddr *) &server_address,
                     sizeof(struct sockaddr_un));

 address_length = sizeof(struct sockaddr_un);
 while(i < max_timeout)
 {
 	 i++;
 	 bytes_received = recvfrom(socket_fd, response, 256, MSG_DONTWAIT,
                           (struct sockaddr *) &(server_address),
                           &address_length);
	 if ((bytes_received > 0) || ((bytes_received < 0) && (errno != EAGAIN)))
	 {
		 break;
	 }
	 usleep(500);
 }
 
 close(socket_fd);
 unlink(FPP_CLIENT_SOCKET);
 if(bytes_received > 0)
 {
  response[bytes_received] = '\0';
  printf("%s",response);
 }
 else
 {
 	 printf("false");
 }
}

/*
 *
 */
void Usage(char *appname)
{
	printf("Usage: %s [OPTION...]\n"
"\n"
"fpp is the Falcon Player CLI helper utility.  It can be used to query\n"
"certain information and send commands to a running fppd daemon.\n"
"\n"
"Options:\n"
"  -V                           - Print version information\n"
"  -s                           - Get fppd status\n"
"  -v VOLUME                    - Set volume to 'VOLUME'\n"
"  -p PLAYLISTNAME [STARTITEM]  - Play Playlist PLAYLISTNAME in repeat mode\n"
"  -P PLAYLISTNAME [STARTITEM]  - Play Playlist PLAYLISTNAME once, optionally\n"
"                                 starting on item STARTITEM in the playlist\n"
"  -c PLAYLIST_ACTION           - Perform a playlist action.  Actions include:\n"
"                                 next     - skip to next item in the playlist\n"
"                                 prev     - jump back to previous item\n"
"                                 stop     - stop the playlist immediately\n"
"                                 graceful - stop the playlist gracefully\n"
"                                 pause    - pause a sequence (not media)\n"
"                                 step     - single-step a paused sequence\n"
"                                 stepback - step a paused sequence backwards\n"
"  -S                           - Stop Playlist gracefully\n"
"  -L                           - Stop Playlist gracefully after current loop\n"
"  -d                           - Stop Playlist immediately\n"
"  -q                           - Shutdown fppd daemon\n"
"  -r                           - Restart fppd daemon\n"
"  -R                           - Reload schedule config file\n"
"  -e EFFECTNAME[,CH[,LOOP]]    - Start Effect EFFECTNAME with optional\n"
"                                 start channel set to CH and optional\n"
"                                 looping if LOOP is set to 1\n"
"  -E EFFECTNAME                - Stop Effect EFFECTNAME\n"
"  -t Slot#                     - Trigger FPP Command Preset in Slot#\n"
"  -G GPIO MODE                 - Configure the given GPIO to MODE. MODEs include:\n"
"                                 Input    - Set to Input. For PiFace inputs this only enables the pull-up\n"
"                                 Output   - Set to Output. (This is not needed for PiFace outputs)\n"
"                                 SoftPWM  - Set to Software PWM.\n"
"  -g GPIO MODE VALUE           - Set the given GPIO to VALUE applicable to the given MODEs defined above\n"
"                                 VALUE is ignored for Input mode\n"
"  -C FPPCOMMAND ARG1 ARG2 ...  - Trigger the FPP Command\n"
"\n", appname);
}
