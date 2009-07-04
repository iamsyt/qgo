#include "networkconnection.h"
#include "consoledispatch.h"
#include "boarddispatch.h"
#include "talk.h"
#include "gamedialog.h"
#include "room.h"
#include "dispatchregistries.h"
#include "playergamelistings.h"
#include "mainwindow.h"			//don't like so much

#define FRIENDFAN_NOTIFY_DEFAULT	1

NetworkConnection::NetworkConnection() :
default_room(0), console_dispatch(0), qsocket(0)
{
	firstonReadyCall = 1;
	mainwindowroom = 0;
	friendfan_notify_default = FRIENDFAN_NOTIFY_DEFAULT;
	boardDispatchRegistry = 0;
	gameDialogRegistry = 0;
	talkRegistry = 0;

	connectingDialog = 0;
}

/* Maybe this should return an enum, but I'm feeling lazy at the moment,
 * and don't want to define two different connectionState like enums */
 /* FIXME there's a bug here.  If we're waiting for a connection and
  * we have a login dialog open and then we hit disconnect, the login
  * window, even if canceled, can try to call this function, thus
  * crashing */
int NetworkConnection::getConnectionState(void)
{
	switch(connectionState)
	{
		case AUTH_FAILED:
			return ND_BADLOGIN;
		case PASS_FAILED:
			return ND_BADPASSWORD;
		case PROTOCOL_ERROR:
			return ND_PROTOCOL_ERROR;
		case CONNECTED:
			return ND_CONNECTED;
		case CANCELED:
			return ND_USERCANCELED;
		case ALREADY_LOGGED_IN:
			return ND_ALREADYLOGGEDIN;
		case CONN_REFUSED:
			return ND_CONN_REFUSED;
		case HOST_NOT_FOUND:
			return ND_BADHOST;
		case SOCK_TIMEOUT:
			return ND_BADCONNECTION;
		case UNKNOWN_ERROR:
			return ND_BADCONNECTION;
		default:
			return ND_WAITING;
	}
}

void NetworkConnection::setConnected(void)
{
	if(connectingDialog)
	{
		connectingDialog->deleteLater();
		connectingDialog = 0;
	}
	connectionState = CONNECTED;
}

bool NetworkConnection::openConnection(const QString & host, const unsigned short port, bool not_main_connection)
{	
	qsocket = new QTcpSocket();	//try with no parent passed for now
	if(!qsocket)
		return 0;
	//connect signals
	
	connect(qsocket, SIGNAL(hostFound()), SLOT(OnHostFound()));
	connect(qsocket, SIGNAL(connected()), SLOT(OnConnected()));
	connect(qsocket, SIGNAL(readyRead()), SLOT(OnReadyRead()));
	connect(qsocket, SIGNAL(disconnected ()), SLOT(OnConnectionClosed()));
//	connect(qsocket, SIGNAL(delayedCloseFinished()), SLOT(OnDelayedCloseFinish()));
//	connect(qsocket, SIGNAL(bytesWritten(qint64)), SLOT(OnBytesWritten(qint64)));
	connect(qsocket, SIGNAL(error(QAbstractSocket::SocketError)), SLOT(OnError(QAbstractSocket::SocketError)));

	if(qsocket->state() != QTcpSocket::UnconnectedState)
	{
		qDebug("Called openConnection while in state %d", qsocket->isValid());
		return 0;
	}
	//remove asserts later
	Q_ASSERT(host != 0);
	Q_ASSERT(port != 0);

	if(!not_main_connection)
		drawPleaseWait();

	qDebug("Connecting to %s %d...\n", host.toLatin1().constData(), port);
	// assume info.host is clean
	qsocket->connectToHost(host, (quint16) port);
	
	/* If dispatch does not have a UI, the thing that sets the UI
	 * will setupRoomAndConsole */
	/* Tricky now without dispatches... who sets up the UI?
	 * there's always a mainwindow... but maybe things aren't setup? */
	
	/* connectionInfo as a message with those pointers is probably a bad idea */
	return (qsocket->state() != QTcpSocket::UnconnectedState);
}

int NetworkConnection::write(const char * packet, unsigned int size)
{
	if(readyToWrite())
	{
		int len;	
		
       		if ((len = qsocket->write(packet, size)) < 0)
			return len;
	}
	else
		send_buffer.write((unsigned char *)packet, size);
	return 0;
}

/* This is only called by network connection subclasses that
 * want to do things this way. *cough* IGS */
void NetworkConnection::writeFromBuffer(void)
{
	int len, size;
	unsigned char packet[256];	//no commands are more than 255, right now
	while(readyToWrite())
	{
		size = send_buffer.canReadLine();
		if(size > 255)
		{
			//FIXME qWarning, better error?
			qDebug("send buffer line too large !!!!");
			exit(0);
		}
		else if(size == 0)
		{
			setReadyToWrite();
			return;
		}
		send_buffer.readLine(static_cast<unsigned char *>(packet), 255);	
		//size presumably == return value... 
		if ((len = qsocket->write(reinterpret_cast<const char *>(packet), size)) < 0)
			qDebug("Write error!!");	//qWarning FIXME
	}
}

void NetworkConnection::writeZeroPaddedString(char * dst, const QString & src, int size)
{
	int i;
	Q_ASSERT(src.length() <= size);
	for(i = 0; i < src.length(); i++)
		dst[i] = src.toLatin1().constData()[i];
	for(i = src.length(); i < size; i++)
		dst[i] = 0x00;
}

class ServerListStorage & NetworkConnection::getServerListStorage(void)
{
	return mainwindow->getServerListStorage(); 
}

int NetworkConnection::checkForOpenBoards(void)
{
	BoardDispatch * boarddispatch;
	std::map<unsigned int, class BoardDispatch *>::iterator i;
	std::map<unsigned int, class BoardDispatch *> * boardDispatchMap =
		boardDispatchRegistry->getRegistryStorage();
	for(i = boardDispatchMap->begin(); i != boardDispatchMap->end(); i++)
	{
		boarddispatch = i->second;
		if(!boarddispatch->canClose())
			return -1;
	}
	return 0;
}

void NetworkConnection::closeConnection(bool send_disconnect)
{
	if(!qsocket)		//when can this happen?  this function shouldn't be
				//called if we get here!!!
		return;
	
	/* FIXME We also need to close any open dispatches,
	* boards etc., for instance if there was an error.
	* Clearing lists and such would be good to.
	* there's a MainWindow::connexionClosed that does
	* good stuff we should move into somewhere
	* nearby., also what about onError?*/
	
	if(qsocket->state() != QTcpSocket::UnconnectedState)
	{
		if(send_disconnect)
		{
			if(console_dispatch)
				console_dispatch->recvText("Disconnecting...\n");
			qDebug("Disconnecting...");
			sendDisconnect();		//legit?	
		}
		// Close it.
		qsocket->close();
	
		// Closing succeeded, return message
		if (qsocket->state() == QTcpSocket::UnconnectedState)
		{
			//authState = LOGIN;
			//sendTextToApp("Connection closed.\n");
		}
	}
	// Not yet closed? Data will be written and then slot OnDelayClosedFinish() called
	
	//delete qsocket;
	qsocket->deleteLater();		//for safety
	qsocket = 0;
	onClose();
	
	return;
}

void NetworkConnection::onClose(void)
{
	/* This is from old netdispatch, fix me */
	//RoomDispatch * room;
	/* This needs to close all open dispatches */
	/*room = connection->getDefaultRoomDispatch();
	if(room)
	{
	connection->setDefaultRoomDispatch(0);
	delete room;
		//FIXME mainwindowroom = 0;
}*/
	tearDownRoomAndConsole();
}

NetworkConnection::~NetworkConnection()
{
	//feels a little sparse so far... this should probably be super close FIXME
	//qDebug("Destroying connection\n");
	//closeConnection();			//specific impl already calls this
	/* Not sure where to delete qsocket.  Possible OnDelayClosedFinish() thing. */
	//delete qsocket;
	//In case these still exist
	//probably unnecessary?
	if(boardDispatchRegistry)
		qDebug("board dispatch registry unfreed!");
}

void NetworkConnection::setConsoleDispatch(class ConsoleDispatch * c)
{
	console_dispatch = c;
}

QTime NetworkConnection::gd_checkMainTime(TimeSystem s, const QTime & t)
{
	if(s == canadian)
	{
		int seconds = (t.minute() * 60) + t.second();
		if(t.second())
		{
			seconds = (t.minute() * 60);
			return QTime(0, t.minute(), 0);
		}
	}
	return t;
}

PlayerListing * NetworkConnection::getPlayerListingFromFriendFanListing(FriendFanListing & f)
{
	if(!default_room) 
		return NULL;
	else
	{
		if(playerTrackingByID())
		{
			if(f.id == 0)
			{
				PlayerListing * p = default_room->getPlayerListing(f.name);
				if(p)
					f.id = p->id;
				return p;
			}
			else
				return default_room->getPlayerListing(f.id);
		}
		else
			return default_room->getPlayerListing(f.name);
	}
}

/* This will often be overridden by the specific connection but
 * otherwise this will handle local lists.  There's also the
 * possibility of a sync option? But that's almost useless */
/* As far as I can tell IGS has no support for on server lists,
 * so that will be the first add */
/* Note that these can't be called if the listing already has
 * the bit set and server side friends lists will have a
 * separate interface */
void NetworkConnection::addFriend(PlayerListing & player)
{
	if(player.friendFanType == PlayerListing::blocked)
		removeBlock(player);
	else if(player.friendFanType == PlayerListing::watched)
		removeFan(player);
	player.friendFanType = PlayerListing::friended;
	//FIXME presumably they're not already on the list because
	//the popup checked that in constructing the popup menu but...
	friendedList.push_back(new FriendFanListing(player.name, friendfan_notify_default));
	getDefaultRoom()->updatePlayerListing(player);
}

void NetworkConnection::removeFriend(PlayerListing & player)
{
	player.friendFanType = PlayerListing::none;
	std::vector<FriendFanListing * >::iterator i;
	for(i = friendedList.begin(); i != friendedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			delete *i;
			friendedList.erase(i);
			break;
		}
	}
	getDefaultRoom()->updatePlayerListing(player);
}

void NetworkConnection::addFan(PlayerListing & player)
{
	if(player.friendFanType == PlayerListing::friended)
		removeFriend(player);
	else if(player.friendFanType == PlayerListing::blocked)
		removeBlock(player);
	player.friendFanType = PlayerListing::watched;
	watchedList.push_back(new FriendFanListing(player.name, friendfan_notify_default));
	getDefaultRoom()->updatePlayerListing(player);
}

void NetworkConnection::removeFan(PlayerListing & player)
{
	player.friendFanType = PlayerListing::none;
	std::vector<FriendFanListing * >::iterator i;
	for(i = watchedList.begin(); i != watchedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			delete *i;
			watchedList.erase(i);
			break;
		}
	}
	getDefaultRoom()->updatePlayerListing(player);
}

void NetworkConnection::addBlock(PlayerListing & player)
{
	if(player.friendFanType == PlayerListing::friended)
		removeFriend(player);
	else if(player.friendFanType == PlayerListing::watched)
		removeFan(player);
	player.friendFanType = PlayerListing::blocked;
	blockedList.push_back(new FriendFanListing(player.name));
}

void NetworkConnection::removeBlock(PlayerListing & player)
{
	player.friendFanType = PlayerListing::none;
	std::vector<FriendFanListing * >::iterator i;
	for(i = blockedList.begin(); i != blockedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			delete *i;
			blockedList.erase(i);
			break;
		}
	}
}

/* This function checks the local lists for a name and sets
 * appropriate flags.  We may also have it do notifications.
 * Other protocol types can override it to, for instance
 * do nothing or just notify, assuming the flags are
 * set when the player is received, rather than looking them
 * up */
/* Another issue: recvPlayerListing is used to change
 * the status of a player, as in which room they're in
 * we don't want it to say "signed on" every time with
 * that.  So we probably need to have an online flag
 * on the friends listing to see if we've already flagged
 * them FIXME */
void NetworkConnection::getAndSetFriendFanType(PlayerListing & player)
{
	std::vector<FriendFanListing * >::iterator i;
	
	for(i = friendedList.begin(); i != friendedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			if(!(*i)->online)
			{
				(*i)->online = true;
				player.friendFanType = PlayerListing::friended;
				/* We may want to put this somewhere else or have it be dialog
				 * with options, like talk or match.  We might also want
			 	 * to block all notifies while one is in a game FIXME 
			 	 * Also, these messages should be nonblocking !?!?!!!!! FIXME*/
			 	/* And actually, its a big enough deal, i.e., we might want console
			 	 * messages, game blocks, etc., that it makes sense to have some separate
			 	 * class for it that handles the notifications... a notification class... */
#ifdef FIXME
				if((*i)->notify)
					QMessageBox::information(0, tr("Signed on"), tr("%1 has signed on").arg(player.name));
#endif //FIXME
			}
			else if(!player.online)
			{
				(*i)->online = false;
				//they are disconnecting
			}
			return;
		}
	}
	for(i = watchedList.begin(); i != watchedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			player.friendFanType = PlayerListing::watched;
			return;
		}
	}
	for(i = blockedList.begin(); i != blockedList.end(); i++)
	{
		if((*i)->name == player.name)
		{
			player.friendFanType = PlayerListing::blocked;
			return;
		}
	}
	player.friendFanType = PlayerListing::none;
	return;
}

void NetworkConnection::checkGameWatched(GameListing & game)
{
	std::vector<FriendFanListing * >::iterator i;
	for(i = watchedList.begin(); i != watchedList.end(); i++)
	{
		if((*i)->name == game.white_name() || (*i)->name == game.black_name())
		{
			if((*i)->notify)
				QMessageBox::information(0, tr("Match Started!"), tr("Match has started between %1 and %2").arg(game.white_name()).arg(game.black_name()));
			return;
		}
	}
}

void NetworkConnection::drawPleaseWait(void)
{
	QPushButton * cancelConnecting;

	connectingDialog = new QMessageBox(QMessageBox::NoIcon, tr("Please wait"), tr("Connecting..."));
	//connectingDialog->setWindowTitle();
	//connectingDialog->setText();
	cancelConnecting = connectingDialog->addButton(QMessageBox::Cancel);
	connect(cancelConnecting, SIGNAL(clicked()), this, SLOT(slot_cancelConnecting()));
	connectingDialog->show();
	connectingDialog->setFixedSize(180, 100);
}

/* Slots */
void NetworkConnection::slot_cancelConnecting(void)
{
	userCanceled();
	connectingDialog->deleteLater();
	connectingDialog = 0;
}

void NetworkConnection::OnHostFound()
{
	//FIXME useful?
}

void NetworkConnection::OnConnected()
{
	/* Invalid read of size 1 here FIXME 
	 * also prints garbage... */
	if(console_dispatch)
		console_dispatch->recvText(QString("Connected to ") + qsocket->peerAddress().toString() + " " +  QString::number(qsocket->peerPort()));
}

void NetworkConnection::onAuthenticationNegotiated(void)
{
	setupRoomAndConsole();
}

void NetworkConnection::onReady(void)
{
	QSettings settings;
	
	if(settings.value("LOOKING_FOR_GAMES").toBool())
	{
		sendToggle("looking", true);
		mainwindow->getUi()->setLookingMode->setChecked(true);
	}
	else
	{
		sendToggle("looking", false);
		mainwindow->getUi()->setLookingMode->setChecked(false);
	}
	if(settings.value("OPEN_FOR_GAMES").toBool())
	{
		sendToggle("open", true);
		mainwindow->getUi()->setOpenMode->setChecked(true);
	}
	else
	{
		sendToggle("open", false);
		mainwindow->getUi()->setOpenMode->setChecked(false);
	}
}

void NetworkConnection::OnReadyRead()
{
	int bytes = qsocket->bytesAvailable();
	if(bytes > 1)
	{
		unsigned char * packet = new unsigned char[bytes];
		/* If that last byte is a newline... */
		qsocket->read((char *)packet, bytes);
		pending.write(packet, bytes);
		delete[] packet;
		handlePendingData(&pending);
	}
}

void NetworkConnection::OnConnectionClosed() 
{
	/* Without networkdispatch, this now needs to do something
	 * except FIXME only if there's actually been an error.  like
	 * if we change servers and get disconnected, versus if we
	 * disconnect ourself.*/

	// read last data that could be in the buffer WHY??
	//OnReadyRead();
	//authState = LOGIN;
	//sendTextToApp("Connection closed by foreign host.\n");
	/* I'm not yet sure what the best procedure is, but
	 * presumably this handler occurs when we disconnect
	 * or when there's an error.  So we'll have it delete
	 * the main room which can then, upon deletion, notify
	 * the mainwindow_server code to delete the netdispatch
	 * which we'll delete the network connection... if
	 * there is one */
	//if(default_room_dispatch)
	//	delete default_room_dispatch;
}

/*
// Connection was closed from application, but delayed
void IGSConnection::OnDelayedCloseFinish()
{
	qDebug("DELAY CLOSED FINISHED");
	
	authState = LOGIN;
	sendTextToApp("Connection closed.\n");
}
*/

void NetworkConnection::OnError(QAbstractSocket::SocketError i)
{
	/* FIXME These should pop up information boxes as
	 * well as somehow prevent the even attempted sending
	 * of other msgs? perhaps? like disconnect msgs?*/
	switch (i)
	{
		case QTcpSocket::ConnectionRefusedError: qDebug("ERROR: connection refused...");
			if(console_dispatch)
				console_dispatch->recvText("Error: Connection refused!");
			connectionState = CONN_REFUSED;
			break;
		case QTcpSocket::HostNotFoundError: qDebug("ERROR: host not found...");
			if(console_dispatch)
				console_dispatch->recvText("Error: Host not found!");
			connectionState = HOST_NOT_FOUND;
			break;
		case QTcpSocket::SocketTimeoutError: qDebug("ERROR: socket time out ...");
			if(console_dispatch)
				console_dispatch->recvText("Error: Socket time out!");
			connectionState = SOCK_TIMEOUT;
			break;
		case QTcpSocket::RemoteHostClosedError: qDebug("ERROR: connection closed by host ...");
			if(console_dispatch)
				console_dispatch->recvText("Error: Connection closed by host!");
			connectionState = PROTOCOL_ERROR;
			break;
		default: qDebug("ERROR: unknown Error...");
			if(console_dispatch)
				console_dispatch->recvText("Error: Unknown socket error!");
			connectionState = UNKNOWN_ERROR;
			break;
	}
	
	if(connectingDialog)
	{
		connectingDialog->deleteLater();
		connectingDialog = 0;
	}
	//sendTextToApp("ERROR - Connection closed.\n"+ qsocket->errorString() );
	qDebug("Socket Error\n");
	//OnReadyRead();
	/* We need to toggle the connection flag, close things up, etc.. */
	
	if(mainwindow)	//mainwindow can ignore if loginDialog is open
		mainwindow->onConnectionError();
}

void NetworkConnection::setupRoomAndConsole(void)
{
	mainwindowroom = new Room();
	mainwindowroom->setConnection(this);
	setDefaultRoom(mainwindowroom);
	
	console_dispatch = new ConsoleDispatch(this);
	setConsoleDispatch(console_dispatch);	
	
	boardDispatchRegistry = new BoardDispatchRegistry(this);
	gameDialogRegistry = new GameDialogRegistry(this);
	talkRegistry = new TalkRegistry(this);
	loadfriendsfans();
}

void NetworkConnection::tearDownRoomAndConsole(void)
{
	savefriendsfans();
	delete boardDispatchRegistry;
	delete gameDialogRegistry;
	delete talkRegistry;
	boardDispatchRegistry = 0;
	gameDialogRegistry = 0;
	talkRegistry = 0;
	
	if(mainwindowroom)
	{
		delete mainwindowroom;
		mainwindowroom = 0;
	}
	if(console_dispatch)
	{
		delete console_dispatch;
		console_dispatch = 0;
	}
}

/* Edit friends/fans list window is created when needed.
 * but we might consider having it always existing and then
 * show hide it?  That's ugly though.  But like these functions
 * should probably be part of a friendsfan class instead of
 * the network connection awkward but perhaps minor FIXME */
void NetworkConnection::loadfriendsfans(void)
{
	QSettings settings;
	int size, i;
	if(!supportsFriendList())
	{
		size = settings.beginReadArray("FRIENDEDLIST");
		for (i = 0; i < size; ++i) 
		{
			settings.setArrayIndex(i);
			friendedList.push_back(new FriendFanListing(
							settings.value("name").toString(),
							settings.value("notify").toBool()));
		}
 		settings.endArray();
	}
	if(!supportsFanList())
	{
		size = settings.beginReadArray("WATCHEDLIST");
		for (i = 0; i < size; ++i) 
		{
			settings.setArrayIndex(i);
			watchedList.push_back(new FriendFanListing(
							settings.value("name").toString(),
							settings.value("notify").toBool()));
		}
 		settings.endArray();
	}
	if(!supportsBlockList())
	{
		size = settings.beginReadArray("BLOCKEDLIST");
		for (i = 0; i < size; ++i) 
		{
			settings.setArrayIndex(i);
			blockedList.push_back(new FriendFanListing(
							settings.value("name").toString()));
		}
 		settings.endArray();
	}
}

void NetworkConnection::savefriendsfans(void)
{
	QSettings settings;
	int index;
	if(!supportsFriendList())
	{
		settings.beginWriteArray("FRIENDEDLIST");
		std::vector<FriendFanListing * >::iterator i;
		index = 0;
		for (i = friendedList.begin(); i != friendedList.end(); i++) 
		{
			settings.setArrayIndex(index);
			settings.setValue("name", (*i)->name);
			settings.setValue("notify", (*i)->notify);
			delete (*i);
			index++;
		}
		settings.endArray();
	}
	if(!supportsFanList())
	{
		settings.beginWriteArray("WATCHEDLIST");
		std::vector<FriendFanListing * >::iterator i;
		index = 0;
		for (i = watchedList.begin(); i != watchedList.end(); i++) 
		{
			settings.setArrayIndex(index);
			settings.setValue("name", (*i)->name);
			settings.setValue("notify", (*i)->notify);
			delete (*i);
			index++;
		}
		settings.endArray();	
	}
	if(!supportsBlockList())
	{
		settings.beginWriteArray("BLOCKEDLIST");
		std::vector<FriendFanListing * >::iterator i;
		index = 0;
		for (i = blockedList.begin(); i != blockedList.end(); i++) 
		{
			settings.setArrayIndex(index);
			settings.setValue("name", (*i)->name);
			delete (*i);
			index++;
		}
		settings.endArray();
	}
}

void NetworkConnection::sendConsoleText(const char * text)
{
	//FIXME issue
	if(console_dispatch) 
		console_dispatch->sendText(text);
}

/* I was thinking about breakng up the seeks and rooms by room dispatch, etc.
 * originally, but if we're getting rid of the dispatches and we haven't
 * yet seen anymore interesting use for a room dispatch then, we'll just
 * let the network connection handle it for now. */
 /* Also, I'm not in a hurry to have the sub connections calling mainwindow
  * functions, but it is global and it is awkward to call recvRoomListing within
  * the subconnection FIXME */
void NetworkConnection::recvRoomListing(class RoomListing * r)
{ 
	if(mainwindow) 
		mainwindow->recvRoomListing(*r, true);
	else
		delete r;
}
				
void NetworkConnection::recvSeekCondition(class SeekCondition * s)
{
	if(mainwindow)
		mainwindow->recvSeekCondition(s);
	else
		delete s;
}

void NetworkConnection::recvSeekCancel(void)
{
	if(mainwindow)
		mainwindow->recvSeekCancel();
}

void NetworkConnection::recvSeekPlayer(QString player, QString condition)
{
	if(mainwindow)
		mainwindow->recvSeekPlayer(player, condition);
}

/* FIXME These are really more like netdispatch type functions, but the registries
 * are on the connection right now.  We might want to move them though at
 * some point if that does seem to make more sense in terms of the namespace. */
BoardDispatch * NetworkConnection::getBoardDispatch(unsigned int game_id)
{
	return boardDispatchRegistry->getEntry(game_id);
}

BoardDispatch * NetworkConnection::getIfBoardDispatch(unsigned int game_id)
{
	return boardDispatchRegistry->getIfEntry(game_id);
}

void NetworkConnection::closeBoardDispatch(unsigned int game_id)
{
	boardDispatchRegistry->deleteEntry(game_id);
}

int NetworkConnection::getBoardDispatches(void)
{
	return boardDispatchRegistry->getSize();
}

GameDialog * NetworkConnection::getGameDialog(const PlayerListing & opponent)
{
	return gameDialogRegistry->getEntry(&opponent);
}

GameDialog * NetworkConnection::getIfGameDialog(const PlayerListing & opponent)
{
	return gameDialogRegistry->getIfEntry(&opponent);
}

void NetworkConnection::closeGameDialog(const PlayerListing & opponent)
{
	gameDialogRegistry->deleteEntry(&opponent);
}

MatchRequest * NetworkConnection::getAndCloseGameDialog(const PlayerListing & opponent)
{
	GameDialog * gd = getIfGameDialog(opponent);
	MatchRequest * new_mr = 0;
	if(gd)
	{
		MatchRequest * mr = gd->getMatchRequest();
		new_mr = new MatchRequest(*mr);
		closeGameDialog(opponent);
	}
	else
		qDebug("Couldn't find gamedialog for opponent: %s", opponent.name.toLatin1().constData());
	return new_mr;
}

Talk * NetworkConnection::getTalk(PlayerListing & opponent)
{
	return talkRegistry->getEntry(&opponent);
}

Talk * NetworkConnection::getIfTalk(PlayerListing & opponent)
{
	return talkRegistry->getIfEntry(&opponent);
}

void NetworkConnection::closeTalk(PlayerListing & opponent)
{
	talkRegistry->deleteEntry(&opponent);
}

BoardDispatch * BoardDispatchRegistry::getNewEntry(unsigned int game_id)
{
	return _c->getDefaultRoom()->getNewBoardDispatch(game_id);
}

void BoardDispatchRegistry::onErase(BoardDispatch * boarddispatch)
{
	delete boarddispatch;
}

/* This is because there are several IGS protocol messages
 * that do not list the id of the board so it has to be looked
 * up from other information.
 * This may be the case with other messages we find later, but
 * this is a simple solution. 
 * Fairly simple.  It really hurt my feelings when I realized
 * that the registry template had this nice private registry
 * and I had to add this other function to complicate it.*/
std::map<unsigned int, BoardDispatch *> * BoardDispatchRegistry::getRegistryStorage(void)
{
	return getStorage();
}

GameDialog * GameDialogRegistry::getNewEntry(const PlayerListing * opponent)
{
	return new GameDialog(_c, *opponent);
}

void GameDialogRegistry::onErase(GameDialog * dlg)
{
	dlg->deleteLater();
}

Talk * TalkRegistry::getNewEntry(PlayerListing * opponent)
{
	/* I don't want the talk windows to be slaved to a room, 
	* but at the same time, currently its done in the mainwindow,
	* so we'll have two different functions with a possible
	* room dispatch that can be notified by the talkdispatch */
	/* Since we got rid of the dispatches... I don't know if this
	 * is an issue anymore */
	/*Room * room = _c->getDefaultRoom();
	if(room)
		return new Talk(_c, *opponent, room);
	else*/
		return new Talk(_c, *opponent);
}

void TalkRegistry::onErase(Talk * dlg)
{
	dlg->deleteLater();
}
