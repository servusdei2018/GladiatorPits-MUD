/******************************************************************************
 Program     : The Gladiator Pits.
 Author      : Richard Woolcock (aka KaVir).
 Last Update : 28th April 2000.
 ******************************************************************************
 File        : commands.c
 Raw size    : 5928 bytes (after going through Erwin's line counter).
 Description : The commands for the Gladiator Pits mud.
 ******************************************************************************
 This code is copyright (C) 2000 by Richard Woolcock.  It may be used and
 distributed freely, as long as you don't remove this copyright notice.
 ******************************************************************************/

CMD(Quit)
{
   /* Exit message, if required (had to remove due to space restrictions) */

   /* Close down the socket and tidy up */
   CloseConnection(pstConn);
}

CMD(Shutdown)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: shutdown <password>\n\r");

   if(strcmp(szTxt,"topsecret"))
      PutOutput(pstConn,TO_USER,"Incorrect password.\n\r");
   else
   {
      /* Shutdown message */
      PutOutput(pstConn,TO_ALL,"[Shutdown by %s]\n\r",
         pstConn->pstBody->a_chName);
      Log("Shutdown by %s.\n\r",pstConn->pstBody->a_chName);

      /* Toggle the flag used within the GameLoop function */
      bShutdown=TRUE;
   }
}

CMD(Create)
{
   int i=0;

   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: create <name>\n\r");

   if(strlen(szTxt)<3||strlen(szTxt)>15)
      PutOutput(pstConn,TO_USER,"Names must be 3-15 letters long.\n\r");
   else if(FindPerson(szTxt))
      PutOutput(pstConn,TO_USER,"That name is in use.\n\r");
   else if(!(pstConn->pstBody=(body_t*)malloc(sizeof(body_t))))
      Log("BUG: Cannot malloc.\n");
   else/* The name was acceptable and a body was created */
   {
      do /* loop through the name */
      {
         /* Make sure that the name only contains letters */
         if (!isalpha(szTxt[i]))
         {
            PutOutput(pstConn,TO_USER,"Name must only contain letters.\n\r");
            return;
         }

         /* Convert the letters to lower case, if they're not already */
         szTxt[i]|=32;
      }
      while(szTxt[++i]);

      /* Make the first letter of the name uppercase */
      szTxt[0]-=32;

      /* Look for an existing player file */
      if((pFile=fopen(szTxt,"rb")))
      {
         /* Load the player file */
         fread(pstConn->pstBody,sizeof(body_t),1,pFile);

         /* Close the player file */
         fclose(pFile);

         /* Clear their pointer variable */
         pstConn->pstBody->pstOpponent=NULL;

         /* Set the connection to GLADIATOR status */
         pstConn->eStatus=GLADIATOR;

         /* Start them in the stadium */
         pstConn->pstBody->iRoom=0;

         /* Log the load */
         Log("%s has rejoined.\n",szTxt);
      }
      else/* No player file exists yet for that name */
      {
         /* Clear the body structure */
         *(pstConn->pstBody)=stBodyEmpty;

         /* The connection is now a CITIZEN */
         pstConn->eStatus=CITIZEN;

         /* Initialise the body's stats to 1 */
         for(i=STR;i<=WIT;i++)
            pstConn->pstBody->iStats[i]=1;

         /* Store the name of the body */
         strcpy(pstConn->pstBody->a_chName,szTxt);

         /* Log the new body's creation */
         Log("New person '%s' created.\n",szTxt);
      }

      /* Display the message to the user and others in the room */
      PutOutput(pstConn,TO_USER,"You step from the crowd.\n\r");
      PutOutput(pstConn,TO_ROOM,"%s steps from the crowd.\n\r",szTxt);
   }
}

CMD(Say)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: say <sentence>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You say '%s'\n\r",szTxt);
   PutOutput(pstConn,TO_ROOM,"%s says '%s'\n\r",NAME(pstConn),szTxt);
}

CMD(Chat)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: chat <sentence>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You chat '%s'\n\r",szTxt);
   PutOutput(pstConn,TO_WORLD,"%s chats '%s'\n\r",NAME(pstConn),szTxt);
}

CMD(Emote)
{
   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: emote <action>\n\r");

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"%s %s\n\r",NAME(pstConn),szTxt);
   PutOutput(pstConn,TO_ROOM,"%s %s\n\r",NAME(pstConn),szTxt);
}

CMD(Commands)
{
   int i,iCount=0;

   /* Loop through all the commands in the command table */
   for(i=0;kstCmdTable[i].szCommand!=NULL;i++)
   {
      /* Display the command if the connection has access to it */
      if((pstConn->eStatus&kstCmdTable[i].eType))
         PutOutput(pstConn,TO_USER,"%-20s%s",kstCmdTable[i].szCommand,
            (!(++iCount%4))?"\n\r":"");
   }

   /* Add an extra newline if necessary, for formatting purposes */
   if ((iCount%4))
      PutOutput(pstConn,TO_USER,"\n\r");
}

CMD(Who)
{
   conn_t*pstUser;
   body_t*pstBody;
   int iCrowd=0,iCount=0;

   /* Loop through all of the connections */
   for(pstUser=pstFirstConn;pstUser;pstUser=pstUser->pstNext)
   {
      /* Check if the appropriate connection has a body */
      if ((pstBody=pstUser->pstBody)!=NULL)
      {
         /* If it DOES have a body, display it's name, wins, losses and kills */
         PutOutput(pstConn,TO_USER,"%-20s (Won:%d Lost:%d Kills:%d)\n\r", 
            pstBody->a_chName,pstBody->iWin,pstBody->iLoss,pstBody->iKill);

         /* Add one to the "people who have bodies" counter */
         iCount++;
      }
      else /* Add one to the "people who don't have bodies" counter */
         iCrowd++;
   }

   /* Display the total number of people with and without bodies */
   PutOutput(pstConn,TO_USER,"Total of %d visible %s, with %d other %s in the crowd.\n\r",
      iCount,iCount==1?"person":"people",iCrowd,iCrowd==1?"person":"people");
}

CMD(Challenge)
{
   conn_t*pstUser;

   /* If there is no input with the command, send a message and return */
   NO_INPUT("Syntax: challenge <gladiator>\n\r");

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   if (!(pstUser=FindPerson(szTxt))||pstUser==pstConn)
      PutOutput(pstConn,TO_USER,"No such gladiator as '%s'.\n\r",szTxt);
   else if(pstUser->eStatus!=GLADIATOR&&pstUser->eStatus!=CHALLENGER)
      PutOutput(pstConn,TO_USER,"They're no gladiator!\n\r");
   else if (ROOM(pstConn)||ROOM(pstUser))
      PutOutput(pstConn,TO_USER,"You must both be in the stadium.\n\r");
   else
   {
      /* Send the challenge message */
      PutOutput(pstConn,TO_USER,"You challenge %s to a fight.\n\r",szTxt);
      PutOutput(pstUser,TO_USER,"%s challenges you to a fight.\n\r",
         pstConn->pstBody->a_chName);

      /* Indicate that you're now the challenger */
      pstConn->eStatus=CHALLENGER;
      pstUser->eStatus=GLADIATOR;

      /* If they've been challenged already, clear the old one */
      if (pstUser->pstBody->pstOpponent)
         pstUser->pstBody->pstOpponent->pstBody->pstOpponent=NULL;

      /* Set the two players as opponents */
      pstConn->pstBody->pstOpponent=pstUser;
      pstUser->pstBody->pstOpponent=pstConn;
   }
}

CMD(Accept)
{
   conn_t*pstUser;
   static int s_iArena;

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   if (!(pstUser=pstConn->pstBody->pstOpponent))
      PutOutput(pstConn,TO_USER,"You've not been challenged.\n\r");
   else
   {
      /* Send the acceptance message */
      PutOutput(pstConn,TO_USER,"Ok.\n\r");
      PutOutput(pstUser,TO_USER,"%s accepts your challenge!\n\r",
         pstConn->pstBody->a_chName);
      PutOutput(pstConn,TO_ALL,"[%s and %s have entered the arena]\n\r",
         pstConn->pstBody->a_chName,pstUser->pstBody->a_chName);

      /* Heal both gladiators */
      pstConn->pstBody->iDam=pstUser->pstBody->iDam=0;

      /* Reset both speed counters */
      pstConn->pstBody->iSpeed=pstUser->pstBody->iSpeed=0;

      /* Move both gladiators into an arena */
      pstConn->pstBody->iRoom=pstUser->pstBody->iRoom=++s_iArena;

      /* Set both gladiators to fighting */
      pstConn->eStatus=pstUser->eStatus=FIGHTING;
   }
}

CMD(Train)
{
   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   /* The connection is now in TRAINING */
   pstConn->eStatus=TRAINING;

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You walk to the training room.  To get back, type 'leave'.\n\r");
   PutOutput(pstConn,TO_ROOM,"%s walks to the training room.\n\r",
      pstConn->pstBody->a_chName);

   /* Indicate that the connection is in the training room (-1) */
   pstConn->pstBody->iRoom=-1;

   /* Display the message to the people already in the training room */
   PutOutput(pstConn,TO_ROOM,"%s enters the training room.\n\r", 
      pstConn->pstBody->a_chName);
}

CMD(Leave)
{
   int i,iCount=0;

   NEED_BODY; /* Logs an error and returns, if pstConn has no body */

   /* Calculate their total number of points worth of stats */
   for(i=STR;i<=WIT;i++)
      iCount+=pstConn->pstBody->iStats[i];

   /* Display the message to the user and others in the room */
   PutOutput(pstConn,TO_USER,"You leave the training room.\n\r");
   PutOutput(pstConn,TO_ROOM,"%s leaves the training room.\n\r", 
      pstConn->pstBody->a_chName);

   /* Indicate that the connection is now back at the stadium (0) */
   pstConn->pstBody->iRoom=0;

   /* Display the message to the people already at the stadium */
   PutOutput(pstConn,TO_ROOM,"%s arrives from the training room.\n\r", 
      pstConn->pstBody->a_chName);

   /* If they've spent all their points, they become a gladiator */
   if(iCount>=MAX_TRAIN)
   {
      pstConn->eStatus=GLADIATOR;

      /* Gladiators can save! */
      Save(pstConn->pstBody);
   }
   else/* Otherwise they're still a CITIZEN */
      pstConn->eStatus=CITIZEN;
}

CMD(Str)
{
   /* Increase the connection's strength, if possible */
   TrainStat(pstConn,"strength",STR);
}

CMD(Dex)
{
   /* Increase the connection's dexterity, if possible */
   TrainStat(pstConn,"dexterity",DEX);
}

CMD(Sta)
{
   /* Increase the connection's stamina, if possible */
   TrainStat(pstConn,"stamina",STA);
}

CMD(Siz)
{
   /* Increase the connection's size, if possible */
   TrainStat(pstConn,"size",SIZ);
}

CMD(Wit)
{
   /* Increase the connection's wits, if possible */
   TrainStat(pstConn,"wits",WIT);
}

CMD(Score)
{
   body_t*pstBody=pstConn->pstBody;

   /* Display the score to the connection */
   PutOutput(pstConn,TO_USER,
      "<<<<<===--------[ Score ]--------===>>>>>\n\r"
      "    Name:%-15s Status:%s\n\r"
      "       Win-Loss-Kill: %2d - %2d - %2d\n\r"
      "<<<<<===-------------------------===>>>>>\n\r"
      "      Str:%d Dex:%d Sta:%d Siz:%d Wit:%d\n\r"
      "           Att:%-2d Def:%-2d Dam:%2d\n\r"
      "          Wounds:%-2d (%-2d) Speed:%d\n\r"
      "<<<<<===-------------------------===>>>>>\n\r",
      pstBody->a_chName,
      StatusName(pstBody),
      pstBody->iWin,
      pstBody->iLoss,
      pstBody->iKill,
      pstBody->iStats[STR],
      pstBody->iStats[DEX],
      pstBody->iStats[STA],
      pstBody->iStats[SIZ],
      pstBody->iStats[WIT],
      ATTACK(pstBody),
      DEFENCE(pstBody),
      DAMAGE(pstBody),
      pstBody->iDam,
      HEALTH(pstBody),
      SPEED(pstBody));
}

