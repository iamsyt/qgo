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


#include <QtGui>
#include "login.h"
#include "mainwindow.h"
#include "mainwindow_settings.h"		//for hostlist
#include "network/igsconnection.h"
#include "network/wing.h"
#include "network/lgs.h"
#include "network/cyberoroconnection.h"
#include "network/tygemconnection.h"
#include "network/eweiqiconnection.h"
#include "network/tomconnection.h"

LoginDialog::LoginDialog(const QString & s, HostList * h)
{
	ui.setupUi(this);
	connectionName = s;
	connection = 0;
	serverlistdialog_open = false;
	
	setWindowTitle(connectionName);
	
	connect(ui.connectPB, SIGNAL(clicked()), this, SLOT(slot_connect()));
	connect(ui.cancelPB, SIGNAL(clicked()), this, SLOT(slot_cancel()));
	
	hostlist = h;
	int firstloginitem = 1;
	for(HostList::iterator hi = hostlist->begin(); hi != hostlist->end(); hi++)
	{
		if((*hi)->host() == connectionName)
		{
			ui.loginEdit->addItem((*hi)->loginName());
			if(firstloginitem)
			{
				if((*hi)->password() != QString())
				{
					ui.passwordEdit->setText((*hi)->password());
					ui.savepasswordCB->setChecked(true);
				}
				firstloginitem = 0;
			}
		}
	}
	connect(ui.loginEdit, SIGNAL(editTextChanged(const QString &)), this, SLOT(slot_editTextChanged(const QString &)));
}

void LoginDialog::slot_connect(void)
{
	//if(ui.connectPB->isDown())	//wth?  unreliable?
	if(serverlistdialog_open)
		return;
	if(ui.loginEdit->currentText() == QString())
	{
		QMessageBox::information(this, tr("Empty Login"), tr("You must enter a username"));
		return;
	}
	hide();

	serverlistdialog_open = true;
	connection = newConnection(serverStringToConnectionType(connectionName), ui.loginEdit->currentText(), ui.passwordEdit->text());
	/* Its awkward to do this here FIXME, just make sure that, for instance
	 * the connection has the mainwindow set within the network dispatch so its
	 * not passing information into nothing */
	/* Another possibility, if we always want netdispatch and mainwindow
	 * connected, setMainWindow should setNetworkDispatch... but maybe
	 * there should be at least the possibility of multiple netdispatches
	 * somehow and we still need to fix the closeConnection stuff
	 * maybe?  Figure out what should logically close what, and when */
	mainwindow->setNetworkConnection(connection);
	//netdispatch->setMainWindow(mainwindow);
	/* We need to wait here to get authorization confirm, no errors,
	   maybe popup either "please wait" dialog, which we'd annoyingly
	   have to handle and then close which might require a separate
	   dialog class, or just have the connect button grayed out or down.*/
	int connectionStatus;
	while((connectionStatus = connection->getConnectionState()) == ND_WAITING)
		QApplication::processEvents(QEventLoop::AllEvents, 300);
	serverlistdialog_open = false;

	if(connectionStatus == ND_BADPASSWORD)
	{
		QMessageBox::information(this, tr("Bad Password"), tr("Invalid Password"));
	}
	else if(connectionStatus == ND_BADLOGIN)
	{
		QMessageBox::information(this, tr("Bad Login"), tr("Invalid Login"));
	}
	else if(connectionStatus == ND_ALREADYLOGGEDIN)
	{
		/* FIXME possibly either here or in network specific code, we want to
		 * prompt to disconnect the other account, or just do it automatically */
		QMessageBox::information(this, tr("Already Logged In"), tr("Are you logged in somewhere else?"));
	}
	else if(connectionStatus == ND_BADCONNECTION || connectionStatus == ND_BADHOST)
	{
		QMessageBox::information(this, tr("Can't connect"), tr("Can't connect to host!"));
	}
	else if(connectionStatus == ND_CONN_REFUSED)
	{
		QMessageBox::information(this, tr("Connection Refused"), tr("Server may be down"));
	}
	else if(connectionStatus == ND_PROTOCOL_ERROR)
	{
		QMessageBox::information(this, tr("Protocol Error"), tr("Check for qGo update"));
	}
	else if(connectionStatus == ND_CONNECTED)
	{
		for(HostList::iterator hi = hostlist->begin(); hi != hostlist->end(); hi++)
		{
			if((*hi)->host() == connectionName)
			{
				if((*hi)->loginName() == ui.loginEdit->currentText())
				{
					delete (*hi);
					hostlist->erase(hi);
					break;
				}
			}
		}
		Host * h = new Host(connectionName, ui.loginEdit->currentText(), 
			(ui.savepasswordCB->isChecked() ? ui.passwordEdit->text() : QString()));
			hostlist->insert(0, h);
		//SUCCESS
		done(1);
		return;
	}
	else if(connectionStatus == ND_USERCANCELED)
	{
		/* I think login is now responsible for mediating that
		 * first connection netdispatch, calls mainwindow
		 * which will close the connection.  FIXME, responsibilities
		 * are not clear even if they work out. */
		show();
	}
	//connection->onError();
	mainwindow->setNetworkConnection(0);
	delete connection;
	connection = 0;
	
}

void LoginDialog::slot_cancel(void)
{
	if(connection)
		connection->userCanceled();
	done(0);
}

void LoginDialog::slot_editTextChanged(const QString & text)
{
	/* This is a little clumsy but its a short list, probably less than 20 */
	for(HostList::iterator hi = hostlist->begin(); hi != hostlist->end(); hi++)
	{
		if((*hi)->host() == connectionName)
		{
			if((*hi)->loginName() == text)
			{
				if((*hi)->password() != QString())
				{
					ui.passwordEdit->setText((*hi)->password());
					ui.savepasswordCB->setChecked(true);
				}
				else
				{
					ui.passwordEdit->setText(QString());
					ui.savepasswordCB->setChecked(false);
				}
				return;
			}
		}
	}
	ui.passwordEdit->setText(QString());
}

ConnectionType LoginDialog::serverStringToConnectionType(const QString & s)
{
	if(s == "IGS")
		return TypeIGS;
	else if(s == "WING")
		return TypeWING;
	else if(s == "LGS")
		return TypeLGS;
	else if(s == "CyberORO")
		return TypeORO;
	else if(s == "Tygem")
		return TypeTYGEM;
	else if(s == "eWeiQi")
		return TypeEWEIQI;
	else if(s == "Tom")
		return TypeTOM;
	else
		return TypeUNKNOWN;
}

QString LoginDialog::connectionTypeToServerString(const ConnectionType c)
{
	switch(c)
	{
		case TypeIGS:
			return "IGS";
			break;
		case TypeWING:
			return "WING";
			break;
		case TypeLGS:
			return "LGS";
			break;
		case TypeORO:
			return "CyberORO";
			break;
		case TypeTYGEM:
			return "Tygem";
			break;
		case TypeEWEIQI:
			return "eWeiQi";
			break;
		case TypeTOM:
			return "Tom";
			break;
		default:
			return "Unknown";
			break;
	}
}

NetworkConnection * LoginDialog::newConnection(ConnectionType connType, QString username, QString password)
{
	switch(connType)	
	{
		case TypeIGS:
			return new IGSConnection(username, password);
		case TypeORO:
			return new CyberOroConnection(username, password);
		case TypeWING:
			return new WingConnection(username, password);
		case TypeLGS:
			return new LGSConnection(username, password);
		case TypeTYGEM:
			return new TygemConnection(username, password);
		case TypeEWEIQI:
			return new EWeiQiConnection(username, password);
		case TypeTOM:
			return new TomConnection(username, password);
		default:
			qDebug("Bad connection Type");
			// ERROR handling???
			return 0;
	}
}
