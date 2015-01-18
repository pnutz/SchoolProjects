/*  SOURCE FILE:      packet.h - A header file that defines Packet and WindowSegment structs
--
--  PROGRAM:          tcp_svr, tcp_clnt, tcp_network
--
--  FUNCTIONS:        Typedef
--
--  DATE:             December 2, 2014
--
--  REVISIONS:        (Date and Description)
--
--  DESIGNERS:        Christopher Eng
--
--  PROGRAMMERS:      Christopher Eng
--
--  NOTES:
--  This header file allows programs to access the Packet and WindowSegment structs.
-----------------------------------------------------------------------------*/
#ifndef DATA
#define DATA 0
#endif /* DATA */

#ifndef ACK
#define ACK 1
#endif /* ACK */

#ifndef EOT
#define EOT 2
#endif /* EOT */

#ifndef SYN
#define SYN 3
#endif /* SYN */

#ifndef SYNACK
#define SYNACK 4
#endif /* SYNACK */

#ifndef FIN
#define FIN 5
#endif /* FIN */

#ifndef PAYLOADLEN
#define PAYLOADLEN 512
#endif /* PAYLOADLEN */

typedef struct
{
  int PacketType;
  int SeqNum;
  char data[PAYLOADLEN];
  int DataLength;
  // defined in MSS, not bytes
  int WindowSize;
  int AckNum;
} Packet;

// WindowSegments make up the Window (linked list)
typedef struct WindowSegment
{
  Packet packet;
  struct WindowSegment *next;
  int delay_sec;
  int delay_nsec;
  int sd;
} WindowSegment;

const char* getPacketType(int code)
{
  switch(code)
  {
    case DATA:
      return "DATA";
      break;
    case ACK:
      return "ACK";
      break;
    case EOT:
      return "EOT";
      break;
    case SYN:
      return "SYN";
      break;
    case SYNACK:
      return "SYN-ACK";
      break;
    case FIN:
      return "FIN";
      break;
    default:
      return "";
  }
}
