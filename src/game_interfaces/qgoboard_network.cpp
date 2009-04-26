#include "qgoboard.h"
#include "tree.h"
#include "move.h"
#include "../network/boarddispatch.h"
#include "../network/messages.h"
#include "undoprompt.h"

qGoBoardNetworkInterface::qGoBoardNetworkInterface(BoardWindow *bw, Tree * t, GameData *gd) : qGoBoard(bw, t, gd)
{
	game_Id = QString::number(gd->number);

	bw->getUi()->board->clearData();

	QSettings settings;
	// value 1 = no sound, 0 all games, 2 my games
	playSound = (settings.value("SOUND") != 1);
	
	if (gd->handicap)
	{
		setHandicap(gd->handicap);
		bw->getBoardHandler()->slotNavLast();
	}
	dontsend = false;
	boardTimerId = 0;
	
	// what about review games?  games without timers ??
	
}


void qGoBoardNetworkInterface::sendMoveToInterface(StoneColor c, int x, int y)
{
	if(dontsend)
		return;
	// to prevent double clicking and upsetting servers...
	dontsend = true;
	if(boardwindow->getGamePhase() == phaseScore)
	{
		/* A little awkward... FIXME, why is there no getMyColor()
		 * function ?!?! */
		//also "c" refers most likely to player color, not the stone clicked on
		if(((tree->getCurrent()->getMatrix()->getStoneAt(x, y) == stoneBlack && boardwindow->getMyColorIsWhite()) ||
		   (tree->getCurrent()->getMatrix()->getStoneAt(x, y) == stoneWhite && boardwindow->getMyColorIsBlack()))
		   && boardwindow->getBoardDispatch()->cantMarkOppStonesDead())
		{
			dontsend = false;	//ready to send again
			return;
		}
		MoveRecord * m = new MoveRecord();
		if(tree->getCurrent()->getMatrix()->isStoneDead(x, y))
		{
			if(boardwindow->getBoardDispatch()->unmarkUnmarksAllDeadStones())
			{
					QMessageBox mb(tr("Unmark All?"),
		      			QString(tr("Unmark all your dead stones?\n")),
		      			QMessageBox::Question,
		      			QMessageBox::Yes,
		      			QMessageBox::No | QMessageBox::Escape | QMessageBox::Default,
		      			QMessageBox::NoButton);
						mb.raise();
						//qgo->playPassSound();	//FIXME sound here? chime?

					if (mb.exec() == QMessageBox::No)
					{
						dontsend = false;	//ready to send again
						delete m;
						return;
					}
			}
			m->flags = MoveRecord::UNREMOVE;
		}
		else
			m->flags = MoveRecord::REMOVE;
		m->x = x;
		m->y = y;
		//move number shouldn't matter
		m->color = c;
		boardwindow->getBoardDispatch()->sendMove(m);
		delete m;
		return;
	}
	else
	{
		/* Check validity of move before sending */
		if(!doMove(c, x, y, true))
		{
			QMessageBox::warning(boardwindow, tr("Invalid Move"), tr("Move %1 %2 is invalid").arg(QString::number(x), QString::number(y)));
			dontsend = false;	//okay to send again
			return;
		}
		/* Rerack time before sending our move */
		boardwindow->getClockDisplay()->makeMove(getBlackTurn());
		boardwindow->getBoardDispatch()->sendMove(new MoveRecord(
			tree->getCurrent()->getMoveNumber(), x, y, c));
	}
}

void qGoBoardNetworkInterface::handleMove(MoveRecord * m)
{
	bool move_alteration = false;
	int move_number, move_counter, handicap;
	Move * remember, * last;
	static bool offset_1 = false;
	
	dontsend = false;		//clear the dontsend bit
	
	/* In case we join in score phase */
	if(m->flags == MoveRecord::NONE && boardwindow->getGamePhase() == phaseScore)
		m->flags = MoveRecord::REMOVE;
	
	/*if(m->flags == MoveRecord::NONE ||
		m->flags == MoveRecord::UNDO ||
		m->flags == MoveRecord::PASS ||
		m->flags == MoveRecord::HANDICAP ||
	  	m->flags == MoveRecord::TERRITORY)*/
		move_alteration = true;
	
	if(move_alteration)	//paired with exit of function
	{
		remember = tree->getCurrent();
		last = tree->findLastMoveInMainBranch();
		
		/*qDebug("Remember: %d %d-%d %p Last: %d %d-%d %p",
		       remember->getMoveNumber(), remember->getX(), remember->getY(),
		       remember->getMatrix(),
		       last->getMoveNumber(), last->getX(), last->getY(),
		       last->getMatrix());*/
		tree->setCurrent(last);

		move_number = m->number;
		//bool hcp_move = tree->getCurrent()->isHandicapMove();
		move_counter = tree->getCurrent()->getMoveNumber();
		if(move_number == NOMOVENUMBER)	//not all services number
			move_number = move_counter;
		/* If move_counter == 0 even though a handicap has been set, there's
		 * a problem */
		//qDebug("MN: %d MC: %d", move_number, move_counter);
		if(move_number > 1 && move_counter == 0)
		{
			/* This is a bit ugly and I still want to rewrite this whole
			 * function.  But basically, if we get a move before we've
			 * retrieved the boardstate, then I guess, and this is
			 * really the thing that should be fixed, not this, but
			 * the offset_1 flag below gets screwed up
			 * such that the first move is skipped if we don't return here*/
			qDebug("Received move before move list, ignoring");
			if (remember != last)
				tree->setCurrent(remember);
			return;
		}
		//1 0
		handicap = boardwindow->getGameData()->handicap;
		/* Since we don't send the handicap move right now... */
		if(move_counter == 0)
		{
			if(handicap/* && move_number == 0*/)	//1??
			{	
				/* Why would we do this?  Its done on the creation.
				 * WING is an issue, but we'll figure that out later 
				 * IGS needs this.  If we, for instance, get the
				 * handicap after the board is created, then
				 * we might set it here... */
#ifdef OLD
				/* This definitely was necessary for IGS
				 * when we were doing !handicap. 
				 * we don't have the handicap available but
				 * we have it before the first move and
				 * there's often a handicap move,
				 * right now, we're relying on HANDICAP */
				qDebug("Setting handicap to %d\n", handicap); 
				setHandicap(handicap);
				// FIXME do we need to test remember here like this?
				if (remember != last)
					tree->setCurrent(remember);
				boardwindow->getBoardHandler()->updateMove(tree->getCurrent());
#endif //OLD
			}
			/* If we never got a move number 0, whether there
			 * was a handicap or not, i.e., move counter is
			 * still set to 0 and presumably it would have
			 * incremented, then we're just going to offset
			 * everything starting here */
			if(move_number == 1)
				offset_1 = true;
			//else if(move_counter == 0)
			//	move_counter++;
		}
		/* This is insanely ugly: setHandicap should properly update the
		 * move counter */
		//if(handicap)
		//	move_counter++;
		if(offset_1)
			move_counter++;
	}
	switch(m->flags)
	{
		case MoveRecord::TERRITORY:
			if(m->color == stoneBlack)
				boardwindow->qgoboard->addMark(m->x, m->y, markTerrBlack);
			else
				boardwindow->qgoboard->addMark(m->x, m->y, markTerrWhite);
			break;
		case MoveRecord::UNDO_TERRITORY:
		{
			int boardsize = tree->getCurrent()->getMatrix()->getSize();
			for(int i = 1; i < boardsize + 1; i++)
			{
				for(int j = 1; j < boardsize + 1; j++)
				{
					if(tree->getCurrent()->getMatrix()->isStoneDead(i, j))
						boardwindow->qgoboard->markLiveStone(i, j);
				}
			}
			break;
		}
		case MoveRecord::REQUESTUNDO:
		{
			qDebug("Got undo message in network interface!!\n");
			BoardDispatch * dispatch = boardwindow->getBoardDispatch();
			//move_number = mv_counter - 1;
			QString opp = dispatch->getOpponentName();
			UndoPrompt * up = new UndoPrompt(&opp,
							 dispatch->supportsMultipleUndo(),
							 m->number);
			
			int up_return = up->exec();
			if (up_return != -1)
			{
				dispatch->sendMove(new MoveRecord(m->number, MoveRecord::UNDO));
			}
			else
			{
				dispatch->sendMove(new MoveRecord(m->number, MoveRecord::REFUSEUNDO));
			}
		}
			break;
		case MoveRecord::UNDO:
			{
			/* Are we supposed to make a brother node or something ??? FIXME 
			* qgoboard.cpp also does this.*/
			BoardDispatch * dispatch = boardwindow->getBoardDispatch();
			if(dispatch->supportsMultipleUndo())
			{
				/* FIXME, really hate the offset_1 thing... 
				 * that should be netcode side */
				while(move_counter > move_number + (offset_1 ? 1 : 0))
				{
					tree->undoMove();
					move_counter--;
				}
			}
			else
				tree->undoMove();
			/* I've turned off multiple undo for tygem, just for now... 
			 * since NOMOVENUMBER FIXME */
			qDebug("Undoing move %d = %d - 1", move_number, move_counter);
			/* FIXME This can get screwy especially around the scoreMode
			 * stuff.... apparently we can only undo our own passes
			 * unlike other WING moves and it only takes 2 undos, not
			 * 3... as if the first undo just gets you out of the 
			 * score place. */
			if(boardwindow->getGamePhase() == phaseScore)
			{
				/* Not sure if this is always true, but it appears
				 * that the last pass doesn't count as a move or
				 * something, meaning that we should delete twice
				 * for it. This is going to be a problem later
				 * FIXME FIXME FIXME*/
				/* IGS does not leave score mode, ever !!!
				 * nor should anything else do to what scoremode
				 * does. Simply clears some of the dead marks*/
				/* Actually, I'm not sure, its possible that the
				 * glGo client we've been using to test this
				 * doesn't handle undo from score very well.  I've
				 * seen some bugs in it before. */
				tree->undoMove();
				leaveScoreMode();
			}
			}
			break;
		case MoveRecord::PASS:
			
			/* If there's three passes, we should stop the clock,
			 * although some servers might have two pass models
			 * and we need a flag for that */
			//if(!boardwindow->getMyColorIsBlack())	//if we're white
			if(boardwindow->getBoardDispatch()->twoPassesEndsGame())
			{
				if(tree->getCurrent()->isPassMove())	
					enterScoreMode();
			}
			else
			{
				if(tree->getCurrent()->parent &&
				   tree->getCurrent()->isPassMove() &&
				   tree->getCurrent()->parent->isPassMove())
					enterScoreMode();
					//boardwindow->setGamePhase ( phaseScore );	//okay?	
			}
			doPass();
			break;
		case MoveRecord::HANDICAP:
			handicap = boardwindow->getGameData()->handicap;
			//if(!handicap)
			//{
				/* Double usage of x is a little ugly */
				setHandicap(m->x);
			//}
			break;
		case MoveRecord::REMOVE:
			//qDebug("md!! toggling life of %d %d", m->x, m->y);
			boardwindow->qgoboard->markDeadStone(m->x, m->y);
			//tree->getCurrent()->getMatrix()->toggleGroupAt(m->x, m->y);
			//boardwindow->qgoboard->kibitzReceived("removing @ " + pt);
			break;
		case MoveRecord::REMOVE_AREA:
			//FIXME
			boardwindow->qgoboard->markDeadArea(m->x, m->y);
			break;
		case MoveRecord::UNREMOVE_AREA:
			//FIXME
			if(boardwindow->getBoardDispatch()->unmarkUnmarksAllDeadStones())
			{
				/* Not sure where we get the dead groups from, FIXME 
				 * okay, really, we should have a list of dead groups
				 * for each player that can be checked on here.
				 * The thing is, ORO also has such a list that it tracks
				 * and I don't want duplication of that code but right
				 * now I have other things on my mind and I just want to
				 * get this done, so I'm going to do something really
				 * quick, dirty, and awkward here and I'll fix it in
				 * a later version... ignoring the stitch in time
				 * thing.  So cut this out soon.  Note also that it
				 * might make sense to have an evaluation function
				 * in the board code, to do something to every
				 * stone of a type... maybe not.*/
				int boardsize = tree->getCurrent()->getMatrix()->getSize();
				for(int i = 1; i < boardsize + 1; i++)
				{
					for(int j = 1; j < boardsize + 1; j++)
					{
						if(tree->getCurrent()->getMatrix()->isStoneDead(i, j))
						{
							if(tree->getCurrent()->getMatrix()->getStoneAt(i, j) == m->color)
							{
								boardwindow->qgoboard->markLiveArea(i, j);
							}
						}
					}
				}
				
			}
			else
				boardwindow->qgoboard->markLiveArea(m->x, m->y);
			break;
		case MoveRecord::DONE_SCORING:
			/* Not sure we can really use this.  terrBlack and terrWhite
			 * are on the boardHandler and it has its own countScore
			 * function that's decently accurate.  Stones are marked
			 * dead fine except for one issue with handicap (edit stones)
			 * and negative numbers marked as dead.  So basically, 
			 * seems like we should ignore what the server tells us
			 * the score is.  The one possible problem with this is that,
			 * for instance, IGS, WING, etc., mishandle certain edge
			 * territories... meaning conceiveably one could think that
			 * one had won by a couple points when in fact one had lost.
			 * I wanted to do something where marking a space as territory
			 * automatically altered the count, but this would mean writing
			 * a wrapper for addMark that checked for adding and removing
			 * terrMarks before passing the addMark to the matrix code.
			 * Its possible, but I'm not sure its worth it.  We'd still
			 * probably need something like countScore so it would be
			 * changing a lot of stuff and adding a bit of overhead just
			 * to avoid a single issue with broken servers.  */
			/* Can we countScore here anyway?  Just for fun? 
			 * I don't think so, I think if we do, it voids dead stone
			 * removals.*/
			boardwindow->getBoardHandler()->countMarked();
			/*if(boardwindow->getGamePhase() != phaseEnded)
			{
				GameResult res = boardwindow->getBoardHandler()->retrieveScore();
				setResult(res);
			}*/
			break;
		default:
		case MoveRecord::NONE:
			
			if(move_number == move_counter)
			{
				/* FIXME Can we guess at the color here ? */
				if(m->color == stoneNone)
					m->color = (getBlackTurn() ? stoneBlack : stoneWhite);
				if (!doMove(m->color, m->x, m->y))
					QMessageBox::warning(boardwindow, tr("Invalid Move"), tr("The incoming move %1 %2 seems to be invalid").arg(QString::number(m->x), QString::number(m->y)));
				else if(m->color == stoneWhite && !boardTimerId)  //awkward ?  FIXME for always move 1?
				{
					onFirstMove();
				}
			}
			else if(move_number < move_counter)
			{
				/* FIXME, this prevents the next if statement
				* partly, this whole thing is screwy */
				/* IGS, certain games have this remove a legit first
				 * move */
				qDebug("Repeat move after undo?? %d %d", move_number, move_counter);
			}
			/* This is for resetting the stones for canadian
			 * timesystems, its awkward for it to be here...  but
			 * I guess I'm getting lazy. FIXME */
			/* I don't think this matters for ORO because time
			 * comes in right after move.  It might matter for IGS */
			/* For stones, this should be called before the move is sent or here
			 * but with getBlackTurn() negated.  Otherwise, its like our move
			 * gets decremented possibly when they play.  It looks weird... double
			 * check with other time style though. */
			boardwindow->getClockDisplay()->rerackTime(getBlackTurn());
			break;
	}
	if(move_alteration)
	{
		//check whether we should update to the incoming move or not
		if (remember != last)
			tree->setCurrent(remember);
	
		boardwindow->getBoardHandler()->updateMove(tree->getCurrent());
	}
}

void qGoBoardNetworkInterface::sendPassToInterface(StoneColor /*c*/)
{
	/* doPass is called when we receive the move from the server, 
	 * as with moves */
	/* FIXME, we need to make sure the move is valid before we send it!!!!!
	 * but without playing it since we get that from server. */
	
	/* Rerack time before sending our move 
	 * Is this okay here?  Do we need to rerack for passes or what ? FIXME*/
	boardwindow->getClockDisplay()->makeMove(getBlackTurn());
	boardwindow->getBoardDispatch()->sendMove(new MoveRecord(tree->getCurrent()->getMoveNumber(), MoveRecord::PASS));
}

void qGoBoardNetworkInterface::slotSendComment()
{
	QString our_name = boardwindow->getBoardDispatch()->getUsername();
	boardwindow->getBoardDispatch()->sendKibitz(boardwindow->getUi()->commentEdit2->text());

	// why isn't this added to SGF files?? FIXME
	// qGoBoard::kibitzReceived has the code for adding this
	// to the tree and thus to the file, but we shouldn't
	// be calling same code from two different places, so the whole
	// thing should be fixed up.
	our_name.prepend( "(" + QString::number(getMoveNumber()) + ") ");
	/* We shouldn't copy it to msg window if we also receive it from server 
	 * If we don't receive it from server, then that's an issue!! 
	 * hint IGS versus WING FIXME
	 * The likely solution if this is an issue is to block kibitzs
	 * from ourself from the network code and have the append here.*/
	/* FIXME: looks like there's a further issue here.  Namely, looks
	 * like we don't need to copy in observing but we do in our own
	 * matches.  Verify that this is case and come back here and then
	 * void observer chat from us kibitz*/
	/* Again, this is redundant in an ORO observer game, don't know
	 * about match */
	boardwindow->getUi()->commentEdit->append(our_name + ": " + boardwindow->getUi()->commentEdit2->text());
	boardwindow->getUi()->commentEdit2->clear();
}

void qGoBoardNetworkInterface::slotUndoPressed()
{
	int moves = tree->getCurrent()->getMoveNumber();
	//if its our turn - 2?
	if ((getBlackTurn() && boardwindow->getMyColorIsBlack()) ||
		    ((!getBlackTurn()) && boardwindow->getMyColorIsWhite()))
		moves -= 2;
	else
		moves--;
	// might want to prompt anyway FIXME ?
	if(boardwindow->getBoardDispatch()->supportsMultipleUndo())
	{
		UndoPrompt * up = new UndoPrompt(0, true, moves);
		int up_return = up->exec();
		if(up_return == -1)
			return;
		else
			moves = up_return;
	}
	
	boardwindow->getBoardDispatch()->sendMove(new MoveRecord(moves, MoveRecord::REQUESTUNDO));
}

/* Really the ui button disables should be on some gamePhase code FIXME */
void qGoBoardNetworkInterface::slotDonePressed()
{
	boardwindow->getBoardDispatch()->sendMove(new MoveRecord(MoveRecord::DONE_SCORING));
	boardwindow->getUi()->doneButton->setEnabled(false);		//FIXME okay? don't want to send done twice
}

void qGoBoardNetworkInterface::slotResignPressed()
{
	QMessageBox mb(tr("Resign?"),
		      QString(tr("Resign game with %1\n")).arg(boardwindow->getBoardDispatch()->getOpponentName()),
		      QMessageBox::Question,
		      QMessageBox::Yes | QMessageBox::Default,
		      QMessageBox::No | QMessageBox::Escape,
		      QMessageBox::NoButton);
	mb.raise();
//		qgo->playPassSound();

	if (mb.exec() == QMessageBox::Yes)
	{
		boardwindow->getBoardDispatch()->sendMove(new MoveRecord(tree->getCurrent()->getMoveNumber(), MoveRecord::RESIGN));
		boardwindow->getUi()->resignButton->setEnabled(false);		//FIXME okay? don't want to send resign twice
	}
}

void qGoBoardNetworkInterface::adjournGame(void)
{
	QString opp_name = boardwindow->getBoardDispatch()->getOpponentName();
	boardwindow->setGamePhase(phaseEnded);
	qDebug("qgBNI::adjournGame");
	if(opp_name == QString())
	{
		GameData * r = boardwindow->getBoardDispatch()->getGameData();
		if(!r)
			qDebug("No game record on adjourned game");
		else
			QMessageBox::information(boardwindow , tr("Game Adjourned"), r->white_name + tr(" vs. ") + r->black_name + tr(" has been adjourned."));

	}
	else
		QMessageBox::information(boardwindow , tr("Game Adjourned"), tr("Game with ") + opp_name + tr(" has been adjourned."));
	boardwindow->getUi()->adjournButton->setEnabled(false);		//FIXME okay? don't want to send adjourn after adjourn
}

/* Might look nicer if we just set the game phase to ended or
 * something, I'll though we might just have lost connection so... */
void qGoBoardNetworkInterface::stopTime(void)
{
	if(boardTimerId)
		killTimer(boardTimerId);
}
