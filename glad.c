/******************************************************************************
 Program     : The Gladiator Pits.
 Author      : Richard Woolcock (aka KaVir).
 Last Update : 28th April 2000.
 ******************************************************************************
 File        : glad.c
 Raw size    : 10456 bytes (after going through Erwin's line counter).
 Description : The Gladiator Pits mud, not including the commands (which are 
               stored in a separate file).
 ******************************************************************************
 This code is copyright (C) 2000 by Richard Woolcock.  It may be used and
 distributed freely, as long as you don't remove this copyright notice.
 ******************************************************************************/

/******************************************************************************
 Required library files.
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

/******************************************************************************
 Required literals.
 ******************************************************************************/

#define MAX_BUF   4096 /* Storage size used for many char arrays */
#define MAX_TRAIN   30 /* Max number of trains allowed */
#define PORT      3000 /* The port number of the mud */
#define STR          0 /* Strength */
#define DEX          1 /* Dexterity */
#define STA          2 /* Stamina */
#define SIZ          3 /* Size */
#define WIT          4 /* Wits */

/******************************************************************************
 Required macros.
 ******************************************************************************/

#define CMD(f)      void Cmd##f##(conn_t*pstConn,char*szTxt)
#define NO_INPUT(t) if(!szTxt[0]) {PutOutput(pstConn,TO_USER,t);return;}
#define NEED_BODY   if(!pstConn->pstBody) {Log("BUG[%s]: No body!\n",__FUNCTION__);return;}
#define IP(n)      (pstConn->iIp>>(8*n))&255
#define HEALTH(p)  (p->iStats[STR]+(p->iStats[STA]*7)+(p->iStats[SIZ]*3))
#define ATTACK(p)  ((p->iStats[DEX]*2)+p->iStats[SIZ])
#define DEFENCE(p) ((p->iStats[WIT]*2)+p->iStats[DEX])
#define DAMAGE(p)  ((p->iStats[STR]*3)+p->iStats[SIZ])
#define SPEED(p)   (10-(((p->iStats[WIT]*2)+p->iStats[DEX])/3))
#define ROOM(p)    (p->pstBody?p->pstBody->iRoom:0)
#define NAME(p)    (p->pstBody?p->pstBody->a_chName:"Someone in the crowd")
#define D10        (1+(int)(10.0*rand()/(RAND_MAX+1.0)))

/******************************************************************************
 Required types.
 ******************************************************************************/

typedef enum {FALSE,TRUE} bool;

typedef enum {TO_USER,TO_ROOM,TO_WORLD,TO_ALL} out_t;

typedef enum {CROWD=1,CITIZEN=2,TRAINING=4,GLADIATOR=8,
              CHALLENGER=16,FIGHTING=32,ALL=63} status_t;

typedef struct connection conn_t;

typedef struct body body_t;

typedef struct
{
   char       *szCommand;
   void      (*pfFunction)();
   status_t eType;
} cmd_table_t;

/******************************************************************************
 Required operation prototypes.
 ******************************************************************************/

/* Socket functions */
int    InitSocket      (void);
void   CloseSocket     (int iSocket);

/* Input functions */
bool    GetInput        (conn_t*pstConn);
void    ParseInput      (conn_t*pstConn);

/* Output functions */
void    PutOutput       (conn_t*pstConn,out_t peOut,char*szTxt,...);
void    FlushOutput     (conn_t*pstConn);
void    PutPrompt       (conn_t*pstConn);

/* Connection functions */
bool    NewConnection   (int iSocket);
void    CloseConnection (conn_t*pstConn);

/* Game functions */
void    GameLoop        (int iControl);
void    Update          (void);
void    Log             (char*szTxt,...);

/* Mud functions */
void    TrainStat       (conn_t*pstConn,char*szTxt,int iStat);
char   *StatusName      (body_t*pstBody);
conn_t *FindPerson      (char*szTxt);
void    Save            (body_t*pstBody);

/******************************************************************************
 Required global variables.
 ******************************************************************************/

static struct body
{
   char     a_chName[16];
   int      iStats[5];
   int      iSpeed,iRoom,iWin,iLoss,iKill,iDam;
   conn_t  *pstOpponent;
} stBodyEmpty;

static struct connection
{
   body_t  *pstBody;
   status_t eStatus;
   char     a_chInBuf[MAX_BUF],a_chOutBuf[MAX_BUF];
   int      iSocket,iInLen,iOutLen,iIp;
   conn_t  *pstPrev,*pstNext;
} stConnEmpty;

conn_t     *pstFirstConn=NULL; /* Connection list */
fd_set      a_sFd;             /* Array of socket flags */
bool        bShutdown=FALSE;   /* When TRUE, shut mud down */
FILE       *pFile;             /* File pointer */

extern const cmd_table_t kstCmdTable[];

/******************************************************************************
 Commands.
 ******************************************************************************/

/* Include the source code file for the commands */
#include "commands.c"

/* Create the command table */
const cmd_table_t kstCmdTable[] =
{
   {"quit",      CmdQuit,      ALL},
   {"who",       CmdWho,       ALL},
   {"commands",  CmdCommands,  ALL},
   {"shutdown",  CmdShutdown,  ~CROWD},
   {"say",       CmdSay,       ALL},
   {"chat",      CmdChat,      ~CROWD},
   {"emote",     CmdEmote,     ALL},
   {"create",    CmdCreate,    CROWD},
   {"score",     CmdScore,     ~CROWD},
   {"train",     CmdTrain,     CITIZEN},
   {"str",       CmdStr,       TRAINING},
   {"dex",       CmdDex,       TRAINING},
   {"sta",       CmdSta,       TRAINING},
   {"siz",       CmdSiz,       TRAINING},
   {"wit",       CmdWit,       TRAINING},
   {"leave",     CmdLeave,     TRAINING},
   {"challenge", CmdChallenge, GLADIATOR|CHALLENGER},
   {"accept",    CmdAccept,    GLADIATOR},
   {NULL}
};

/******************************************************************************
 Required operations.
 ******************************************************************************/

/* Function: main
 *
 * This is the main function required by C.  It starts up the mud, calls
 * InitSocket to create the control socket, loads up any data files
 * required, then calls the GameLoop function, which is only returned
 * from when the mud is in shutdown state.  At that point the control
 * socket is closed and the program exits.
 *
 * The function takes no parameters.
 *
 * This function has to return an int, which is 0 (unless the mud crashes).
 */
int main()
{
   int iControl; /* Contains the control socket descriptor */

   /* Seed the dice */
   srand(time(0));

   /* Startup message */
   Log("Mud started on port %d.\n",PORT);

   /* Initialise the control socket */
   iControl=InitSocket();

   /* Call the main loop */
   GameLoop(iControl);

   /* Close the main socket */
   close(iControl);

   /* Exit the program */
   return 0;
}


/* Function: InitSocket
 *
 * This function creates the control socket to listen for incoming
 * connections.
 *
 * The function takes no parameters.
 *
 * This function returns an int: The descriptor of the socket created.
 */
int InitSocket()
{
   static struct sockaddr_in s_stSaEmpty; /* Empty structure */
   struct sockaddr_in   stSa=s_stSaEmpty; /* Clear stSa */
   struct linger stLd;
   int iFd,iOpt=1;

   /* Initialise the control socket data */
   stSa.sin_family=AF_INET;
   stSa.sin_addr.s_addr=INADDR_ANY;
   stSa.sin_port=htons(PORT);
   stLd.l_onoff=stLd.l_linger=0;

   /* Create the control socket and stores it's descriptor in iFd */
   iFd=socket(AF_INET,SOCK_STREAM,0);

   /* Set the socket options */
   setsockopt(iFd,SOL_SOCKET,SO_REUSEADDR,(char*)&iOpt,sizeof(iOpt));
   setsockopt(iFd,SOL_SOCKET,SO_LINGER,&stLd,sizeof(stLd));

   /* Assign the name (stSa) to the control socket (iFd) */
   bind(iFd,(struct sockaddr*)&stSa,sizeof(stSa));

   /* Allow iFd to accept incoming connections, with up to 3 pending */
   listen(iFd,3); /* Backlog 3, max is 5 in BSD, SOMAXCONN in Linux */

   /* Return the description of the control socket */
   return iFd;
}


/* Function: CloseSocket
 *
 * This function closes the specified socket.
 *
 * The function takes one parameter, as follows:
 *
 * iSocket: The socket number to be closed.
 *
 * This function has no return value.
 */
void CloseSocket(int iSocket)
{
   /* Close the iSocket descriptor */
   close(iSocket);

   /* Remove iSocket from the descriptor set */
   FD_CLR(iSocket,&a_sFd);
}


/* Function: GetInput
 *
 * This function reads the input from the connection's socket then calls
 * the ParseInput function to parse the command.
 *
 * The function takes one parameter, as follows:
 *
 * pstConn: The connection to read the input from.
 *
 * This function returns a bool: TRUE means the read was successful, FALSE 
 * means it was not.
 */
bool GetInput(conn_t*pstConn)
{
   char a_chBuf[MAX_BUF]={'\0'}; /* Text storage buffer */
   int  iInput,i;
   bool bReadLine=FALSE;

   /* Read the input from the socket */
   if((iInput=read(pstConn->iSocket,a_chBuf,MAX_BUF))<=0)
      return FALSE; /* Connection was lost while reading */

   /* Store the usable (printing) characters in the connection's input buffer */
   for(i=0; i<iInput && pstConn->iInLen<MAX_BUF-1 && !bReadLine; i++)
   {
      /* Only store the printing characters */
      if(isprint(a_chBuf[i]))
         pstConn->a_chInBuf[pstConn->iInLen++]=a_chBuf[i];
      else if(a_chBuf[i]=='\n')
         bReadLine=TRUE; /* A full input line has been read */
   }

   /* Terminate the connection's input buffer */
   pstConn->a_chInBuf[pstConn->iInLen]='\0';

   /* If a full line has been read in */
   if(bReadLine)
   {
      /* Parse the line of input */
      ParseInput(pstConn);

      /* Display the prompt (unless there was no input) */
      if(pstConn->iInLen)
         PutPrompt(pstConn);

      /* Reset the input buffer */
      pstConn->iInLen=0;
   }

   /* Input was successfully read from the connection */
   return TRUE;
}


/* Function: ParseInput
 *
 * This function parses a line of input and performs the appropriate
 * response.
 *
 * The function takes one parameter, as follows:
 *
 * pstConn: The connection to parse the response command to.
 *
 * This function has no return value.
 */
void ParseInput(conn_t*pstConn)
{
   char *szTxt=pstConn->a_chInBuf;
   int  i;

   /* Loop through the connection's input buffer */
   while(*szTxt)
   {
      /* If a space is found */
      if(*szTxt==' ')
      {
         /* Set the space to a string terminator and break from the loop */
         *szTxt++='\0';
         break;
      }
      szTxt++;
   }

   /* Process commands */
   for (i=0;kstCmdTable[i].szCommand!=NULL;i++)
   {
      if(!strcasecmp(pstConn->a_chInBuf,kstCmdTable[i].szCommand))
      {
         if((pstConn->eStatus&kstCmdTable[i].eType))
         {
            /* The command was found, so perform the appropriate function */
            (*kstCmdTable[i].pfFunction)(pstConn,szTxt);
            return; /* No point carrying on */
         }
      }
   }

   /* Inform the user that there is no such command (unless they just hit enter) */
   if(pstConn->a_chInBuf[0])
      PutOutput(pstConn,TO_USER,"Unrecognised command '%s'.  Type 'commands' to list available options.\n\r",pstConn->a_chInBuf);
   else /* If they just hit enter, send "nothing" (this is required for screen formatting) */
      PutOutput(pstConn,TO_USER,"");
}


/* Function: PutOutput
 *
 * This function sends the specified string to the specified connection.
 *
 * The function takes three parameters, as follows:
 *
 * pstConn: The connection to send the string to.
 * szTxt:   The string to be sent.
 * ...:     One or more other arguments.
 *
 * This function has no return value.
 */
void PutOutput(conn_t*pstConn,out_t peOut,char*szTxt,...)
{
   va_list pArg;             /* Pointer to the next argument */
   bool    bRePrompt=FALSE;
   conn_t *pstUser;
   char    a_chBuf[MAX_BUF];

   /* Variable arguments lists "start" macro */
   va_start(pArg,szTxt);

   /* Store szTxt in a_chBuf as per sprintf, using pArg for arguments */
   vsprintf(a_chBuf,szTxt,pArg);

   /* Loop through all connections */
   for(pstUser=pstFirstConn;pstUser;pstUser=pstUser->pstNext)
   {
      /* Determine who should get sent the text */
      switch(peOut)
      {
         case TO_USER:/* Text goes to just the user */
            if(pstUser==pstConn)break;continue;
         case TO_ROOM:/* Text goes to all in users room - except user */
            if(pstUser!=pstConn&&ROOM(pstConn)==ROOM(pstUser))break;continue;
         case TO_WORLD:/* Text goes to all in world - except user */
            if(pstUser!=pstConn)break;continue;
         case TO_ALL:/* Text goes to all in world - including user */
      }

      /* Determine if their prompt needs to be redrawn first */
      if(!pstUser->iOutLen&&!pstUser->iInLen)
      {
         /* Redraw the prompt */
         pstUser->a_chOutBuf[pstUser->iOutLen++]='\n';
         pstUser->a_chOutBuf[pstUser->iOutLen++]='\r';
         bRePrompt=TRUE;
      }

      /* Set szTxt to points to the start of the reformated text */
      szTxt=a_chBuf;

      /* Display the text to the current connection */
      while(*szTxt)
      {
         pstUser->a_chOutBuf[pstUser->iOutLen++]=*szTxt++;
         if(pstUser->iOutLen>=MAX_BUF-1)
            FlushOutput(pstUser);
      }

      /* Redraw the prompt if required */
      if(bRePrompt)
         PutPrompt(pstUser);
   }

   /* Variable arguments lists "end" macro */
   va_end(pArg);
}


/* Function: FlushOutput
 *
 * This function flushes the output buffer for the specified connection.
 *
 * The function takes one parameter, as follows:
 *
 * pstConn: The connection to be flushed.
 *
 * This function has no return value.
 */
void FlushOutput(conn_t*pstConn)
{
   /* Check that the connection's output buffer isn't empty */
   if(pstConn->iOutLen>0)
   {
      /* Write the content of the output buffer to the socket descriptor */
      write(pstConn->iSocket,pstConn->a_chOutBuf,pstConn->iOutLen);

      /* Indicate that the output buffer is now empty */
      pstConn->iOutLen=0;
   }
}


/* Function: PutPrompt
 *
 * This function displays the prompt.
 *
 * The function takes one parameter, as follows:
 *
 * pstConn: The connection to send the prompt string to.
 *
 * This function has no return value.
 */
void PutPrompt(conn_t*pstConn)
{
   /* Call PutOutput, passing in the prompt text */
   PutOutput(pstConn,TO_USER,"> "); /* Default prompt is "> " */
}


/* Function: NewConnection
 *
 * This function creates a new connection.
 *
 * The function takes one parameter, as follows:
 *
 * iSocket: The new connection's socket number.
 *
 * This function returns a bool: TRUE if the connection could be created,
 * and FALSE if it could not.
 */
bool NewConnection(int iSocket)
{
   struct sockaddr_in stSa;
   int    iSaSize;
   conn_t*pstConn;

   /* Attempt to allocate space for a new connection */
   if(!(pstConn=(conn_t*)malloc(sizeof(conn_t))))
   {
      /* If unable to malloc, tidy up and return FALSE */
      CloseSocket(iSocket);
      Log("BUG: Cannot malloc.\n");
      return FALSE;
   }

   /* Add the new connection to the active socket list */
   FD_SET(iSocket,&a_sFd);

   /* Clear the connection structure */
   *pstConn=stConnEmpty;

   /* New connections are members of the crowd */
   pstConn->eStatus=CROWD;

   /* Insert the new connection into the connection list */
   if(pstFirstConn)
   {
      pstFirstConn->pstPrev=pstConn;
      pstConn->pstNext=pstFirstConn;
   }

   /* The new connection goes on the front of the list */
   pstFirstConn=pstConn;

   /* Initialise the new connection structure */
   pstConn->iSocket=iSocket;

   /* Calculate and store the ip address of the new connection */
   iSaSize=sizeof(stSa);
   getpeername(iSocket,(struct sockaddr*)&stSa,&iSaSize);
   pstConn->iIp=ntohl(stSa.sin_addr.s_addr);

   /* Log the connection */
   Log("Connection from %d.%d.%d.%d\n",IP(3),IP(2),IP(1),IP(0));

   /* Greet the new connection */
   PutOutput(pstConn,TO_USER,"Welcome to the Gladiator Pits!\n\r");

   /* New connection was successfully created, so return TRUE */
   return TRUE;
}


/* Function: CloseConnection
 *
 * This function closes the specified connection.
 *
 * The function takes one parameter, as follows:
 *
 * pstConn: The connection to be closed.
 *
 * This function has no return value.
 */
void CloseConnection(conn_t*pstConn)
{
   /* Must free their body, if they've created one */
   if(pstConn->pstBody)
   {
      /* If they are fighting someone, that person has to stop */
      if(pstConn->pstBody->pstOpponent)
      {
         body_t*pstBody=pstConn->pstBody->pstOpponent->pstBody;

         /* Always clear their opponent (in case of an unaccepted challenge) */
         pstBody->pstOpponent=NULL;

         /* Check whether you quit while actually fighting (a surrender) */
         if(pstConn->eStatus==FIGHTING)
         {
            /* Adjusts wins and losses... */
            pstConn->pstBody->iLoss++;
            pstBody->iWin++;

            /* ...stop the fighting... */
            pstConn->pstBody->pstOpponent->eStatus=GLADIATOR;

            /* ...return them to the stadium... */
            pstBody->iRoom=0;

            /* ...and save both you and them! */
            Save(pstBody);
            Save(pstConn->pstBody);
         }
      }

      PutOutput(pstConn,TO_WORLD,"%s vanishes into the crowd.\n\r",pstConn->pstBody->a_chName);
      free(pstConn->pstBody);
   }

   /* Have a quick flush in case there is any pending output */
   FlushOutput(pstConn);

   /* If the connection was first in the list, reset the start of list */
   if(pstFirstConn==pstConn)
      pstFirstConn=pstConn->pstNext;

   /* Remove the connection from the list */
   if(pstConn->pstPrev)
      pstConn->pstPrev->pstNext=pstConn->pstNext;
   if(pstConn->pstNext)
      pstConn->pstNext->pstPrev=pstConn->pstPrev;

   /* Close and free the socket */
   CloseSocket(pstConn->iSocket);
   free(pstConn);
}


/* Function: GameLoop
 *
 * This function controls the main program flow.  It goes through the
 * connections looking for input, parses any it finds, then sends output
 * back to those connections.
 *
 * The function takes one parameter, as follows:
 *
 * iControl: The control socket.
 *
 * This function has no return value.
 */
void GameLoop(int iControl)
{
   struct  timeval stTv;
   fd_set  sFd;
   int     iFdMax;
   time_t  tTime=0;
   conn_t *pstConn,*pstConnNext;

   /* Work out how many descriptors can be open at once */
   iFdMax=getdtablesize(); /* May want to error trap for -1 */

   /* Clear the descriptor set */
   FD_ZERO(&a_sFd);

   /* Add iControl to the descriptor set */
   FD_SET(iControl,&a_sFd);

   do /* until a shutdown occurs */
   {
      /* Clear the socket flags */
      bcopy((char*)&a_sFd,(char*)&sFd,sizeof(sFd));

      /* Set to 1 second */
      stTv.tv_sec=1;
      stTv.tv_usec=0;

      /* Sleep until a descriptor tries to do something */
      if(select(iFdMax,&sFd,(fd_set*)0,(fd_set*)0,&stTv)<0)
         continue; /* Try again if it raised an exception */

      /* Update pulse called every second */
      if((time(0)-tTime)>=1/* second */)
      {
         Update();
         tTime=time(0);
      }

      /* Check if iControl is in the sFd descriptor set */
      if(FD_ISSET(iControl,&sFd))
      {
         struct sockaddr_in stSa;
         int iNewConnection,iSaSize=sizeof(stSa);
         /* Looking for pending connections on the control socket */
         if((iNewConnection=accept(iControl,(struct sockaddr*)&stSa,&iSaSize))>=0)
            NewConnection(iNewConnection); /* Create a new connection */
      }

      /* Loop through all of the connections */
      for(pstConn=pstFirstConn;pstConn;pstConn=pstConnNext)
      {
         pstConnNext=pstConn->pstNext;

         /* Activity on the socket but no input means dropped connection */
         if (FD_ISSET(pstConn->iSocket,&sFd)&&!GetInput(pstConn))
            CloseConnection(pstConn);
         else /* Otherwise flush the output buffer */
            FlushOutput(pstConn);
      }
   }
   while (!bShutdown);
}


/* Function: Update
 *
 * This function is the pulse routine, called once per second.
 *
 * The function takes no parameters.
 *
 * This function has no return value.
 */
void Update()
{
   conn_t*pstConn,*pstUser,*pstConnNext;
   body_t*pstBody,*pstBody2;
   char*szYou,*szThem;
   int iDam,iResult;

   /* Loop through all of the connections */
   for(pstConn=pstFirstConn;pstConn;pstConn=pstConnNext)
   {
      pstConnNext=pstConn->pstNext;

      /* Initialise current connection's body (may be NULL) */
      pstBody=pstConn->pstBody;

      /* Insert stuff like combat, etc, here */
      if(pstConn->eStatus==FIGHTING)
      {
         if(++pstBody->iSpeed>=SPEED(pstBody))
         {
            /* Reset the speed counter (three second recovery time) */
            pstBody->iSpeed=-3;

            /* Initialise opponent's connection and body */
            pstUser=pstBody->pstOpponent;
            pstBody2=pstUser->pstBody;

            /* Determine the success of the attack */
            iResult=ATTACK(pstBody)+D10+D10-DEFENCE(pstBody2)-D10;

            /* Determine the damage for a successful attack */
            iDam=DAMAGE(pstBody);

            if(iResult<1)
            {
               /* You've missed */
               szYou="miss";
               szThem="misses";
               iDam=0; /* Does no damage at all */
            }
            else if(iResult<6)
            {
               /* A light punch */
               szYou="punch";
               szThem="punches";
               iDam/=2; /* Only does half damage */
            }
            else /* A high-quality success, does full damage! */
            {
               /* A heavy kick */
               szYou="kick";
               szThem="kicks";
            }

            /* Display the appropriate message */
            PutOutput(pstConn,TO_USER,"You %s your opponent!\n\r",szYou);
            PutOutput(pstUser,TO_USER,"Your opponent %s you!\n\r",szThem);

            /* Inflict the damage */
            pstBody2->iDam+=iDam;

            /* See if you've killed them */
            if(pstBody2->iDam>HEALTH(pstBody2))
            {
               /* Send the victory message */
               PutOutput(pstConn,TO_ALL,"[%s has killed %s in the arena!]\n\r",
                  pstBody->a_chName,pstBody2->a_chName);

               /* Wipe the loser's player file */
               remove(pstBody2->a_chName);

               /* Free the loser's body */
               free(pstBody2);

               /* Refresh the loser's connection structure */
               pstUser->pstBody=NULL;
               pstUser->eStatus=CROWD;

               /* Reward the victor... */
               pstBody->iWin++;
               pstBody->iKill++;

               /* ...stop them fighting... */
               pstBody->pstOpponent=NULL;
               pstConn->eStatus=GLADIATOR;

               /* ...return them to the stadium... */
               pstBody->iRoom=0;

               /* ...and save them! */
               Save(pstBody);
            }
         }
      }
   }
}


/* Function: Log
 *
 * This function will send a line of text to the log file, along with
 * the current date and time.
 *
 * The function takes two parameters, as follows:
 *
 * szTxt: A pointer to the string to be logged.
 * ...:   One or more other arguments.
 *
 * This function has no return value.
 */
void Log(char*szTxt,...)
{
   FILE*pFile;           /* File pointer */
   time_t tTime=time(0); /* The current time */
   va_list pArg;         /* Pointer to the next argument */

   /* Variable arguments lists "start" macro */
   va_start(pArg,szTxt);

   /* Open the log file in append mode */
   if((pFile=fopen("log.txt","a")))
   {
      /* Write the date and time to the log file */
      fprintf(pFile,"%.24s: ",ctime(&tTime));

      /* Write the text (including optional arguments) to the log file */
      vfprintf(pFile,szTxt,pArg);

      /* Flush and close the log file */
      fflush(pFile);
      fclose(pFile);
   }

   /* Variable arguments lists "end" macro */
   va_end(pArg);
}


/* Function: TrainStat
 *
 * This function trains a stat.
 *
 * The function takes three parameters, as follows:
 *
 * pstConn: The connection of the player to train.
 * szTxt:  A pointer to the string containing the text form of the stat.
 * iStat:   The number of the stat to be trained.
 *
 * This function has no return value.
 */
void TrainStat(conn_t*pstConn,char*szTxt,int iStat)
{
   int iCount=0,i;

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   /* Calculate their total number of points worth of stats */
   for(i=STR;i<=WIT;i++)
      iCount+=pstConn->pstBody->iStats[i];

   /* Check if they have less than the maximum allowed */
   if(iCount<MAX_TRAIN)
   {
      /* Increment the stat as long as it doesn't take it over 9 */
      if(pstConn->pstBody->iStats[iStat]>=9)
         PutOutput(pstConn,TO_USER,"%s already at max.\n\r",szTxt);
      else
         PutOutput(pstConn,TO_USER,"%s trained to %d (%d points left).\n\r",szTxt,
            ++pstConn->pstBody->iStats[iStat],MAX_TRAIN-1-iCount);
   }
   else/* Not enough points to train any further */
      PutOutput(pstConn,TO_USER,"No remaining points.\n\r");
}


/* Function: StatusName
 *
 * This function returns the status (as a string) of the specified body.
 *
 * The function takes one parameter, as follows:
 *
 * pstBody: The body to find the status of.
 *
 * This function returns a pointer to a string, which contains the
 * textual value of the body's status.
 */
char*StatusName(body_t*pstBody)
{
   int iStatus=pstBody->iWin+pstBody->iKill-pstBody->iLoss;

   /* Make sure iStatus is in the range 0 to 50 */
   iStatus=iStatus<0?0:iStatus>50?50:iStatus;

   /* Return the appropriate name according to how much status they have */
   switch(iStatus/10)
   {
      default:return "Novice";
      case  1:return "Blooded";
      case  2:return "Skilled";
      case  3:return "Veteran";
      case  4:return "Expert";
      case  5:return "Master";
   }
}


/* Function: FindPerson
 *
 * This function returns a pointer to the specified person.
 *
 * The function takes one parameter, as follows:
 *
 * szTxt: The name of the person to find.
 *
 * This function returns a pointer to a structure, which contains the
 * data of the person located - or NULL if they could not be found.
 */
conn_t*FindPerson(char*szTxt)
{
   conn_t*pstUser;

   /* Loop through all of the connections, checking their names (if any) */
   for(pstUser=pstFirstConn;pstUser;pstUser=pstUser->pstNext)
      if(pstUser->pstBody&&!strcasecmp(pstUser->pstBody->a_chName,szTxt))
         return pstUser; /* The person was found */

   return NULL; /* The person was not found */
}


/* Function: Save
 *
 * This function saves the specified body.
 *
 * The function takes one parameter, as follows:
 *
 * pstBody: A pointer to the body to be saved.
 *
 * This function has no return value.
 */
void Save(body_t*pstBody)
{
   /* Try and open the file */
   if((pFile=fopen(pstBody->a_chName,"wb")))
   {
      /* Save the player file */
      fwrite(pstBody,sizeof(body_t),1,pFile);

      /* Close the player file */
      fclose(pFile);
   }
}

