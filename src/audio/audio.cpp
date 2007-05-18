/***************************************************************************
 *   Copyright (C) 2006 by Emmanuel B�ranger   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
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
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#include "audio/audio.h"

#ifdef Q_OS_LINUX
#include "audio/alsa.h"
#endif

QSoundSound::QSoundSound(const QString &filename, QObject *parent)
	: Sound(filename, parent)
{
	qSound = new QSound(filename, this);
}

void QSoundSound::play(void)
{
	qSound->play();
}

Sound *SoundFactory::newSound(const QString &filename, QObject *parent)
{
#ifdef Q_OS_LINUX
	return new QAlsaSound(filename, parent);
#else
	return new QSoundSound(filename, parent);
#endif
}
