/***************************************************************************
 *   Copyright (C) 2009 by The qGo Project                                 *
 *                                                                         *
 *   This file is part of qGo.   					   *
 *                                                                         *
 *   qGo is free software: you can redistribute it and/or modify           *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 *   or write to the Free Software Foundation, Inc.,                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#ifndef ROOM_H
#define ROOM_H

#include <QtGui>

class NetworkConnection;
class BoardDispatch;
class PlayerListing;
class GameListing;
class GameListingRegistry;
class PlayerListingRegistry;
class PlayerListingIDRegistry;
class Talk;
class FilteredView;
class PlayerListModel;
class PlayerSortProxy;
class GamesListModel;
class GamesSortProxy;
class MainWindow;

class Room : public QObject
{
	Q_OBJECT

	public:
		Room();
		void setConnection(NetworkConnection * c);
		virtual ~Room();
		void onError(void);
		void updateRoomStats(void);
		void clearPlayerList(void);
		void clearGamesList(void);
		void talkOpened(Talk * d);
		void recvToggle(int type, bool val);
		PlayerListing * getPlayerListing(const QString & name);
		PlayerListing * getPlayerListing(const unsigned int id);
		PlayerListing * getPlayerListingByNotNickname(const QString & notnickname);
		GameListing * getGameListing(unsigned int key);
		BoardDispatch * getNewBoardDispatch(unsigned int key);
		void recvPlayerListing(class PlayerListing * g);
		void recvExtPlayerListing(class PlayerListing * player);
		void updatePlayerListing(class PlayerListing & player);
		void recvGameListing(class GameListing * g);
		void sendStatsRequest(PlayerListing & opponent);
	protected:
		void setupUI(void);
		GameListing * registerGameListing(GameListing * l);
		void unRegisterGameListing(unsigned int key);
		
		unsigned int players;
		unsigned int games;
		GameListingRegistry * gameListingRegistry;
		PlayerListingRegistry * playerListingRegistry;
		PlayerListingIDRegistry * playerListingIDRegistry;
	private:
		PlayerListModel * playerListModel;
		PlayerSortProxy * playerSortProxy;
		GamesListModel * gamesListModel;
		GamesSortProxy * gamesSortProxy;

		FilteredView * playerView, * gamesView;
		QToolButton * refreshGamesButton, * refreshPlayersButton;
		QPushButton * editFriendsWatchesListButton;
		QComboBox * whoBox1, * whoBox2;
		QCheckBox * whoOpenCheckBox, * friendsCheckBox, * watchesCheckBox;
		NetworkConnection * connection;
		QModelIndex popup_item;
		
		GameListing * popup_gamelisting;
		PlayerListing * popup_playerlisting;
	private slots:
		void slot_playersDoubleClicked(const QModelIndex &);
		void slot_gamesDoubleClicked(const QModelIndex &);
		void slot_refreshPlayers(void);
		void slot_refreshGames(void);
		void slot_setRankSpreadView(void);
		void slot_showOpen(int);
		void slot_showFriends(int);
		void slot_showWatches(int);
		void slot_editFriendsWatchesList(void);
		void slot_showPopup(const QPoint & iPoint);
		void slot_showGamesPopup(const QPoint & iPoint);
		void slot_addFriend(void);
		void slot_removeFriend(void);
		void slot_addWatch(void);
		void slot_removeWatch(void);
		void slot_addBlock(void);
		void slot_popupStats(void);
		void slot_popupMatch(void);
		void slot_popupTalk(void);
		void slot_popupObserveOutside(void);
		void slot_popupJoinObserve(void);	
};

#endif //ROOM_H
