/***************************************************************************
                          skymap.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Sat Feb 10 2001
    copyright            : (C) 2001 by Jason Harris
    email                : jharris@30doradus.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <kconfig.h>
#include <klocale.h>
#include <kurl.h>
#include <kiconloader.h>
#include <kstatusbar.h>
#include <kmessagebox.h>

#include <qlabel.h>
#include <qpopupmenu.h>
#include <qcursor.h>
#include <qpointarray.h>
#include <qfont.h>
#include <qtextstream.h>
#include <qbitmap.h>

#include <math.h>
#include <stdlib.h>
#include <stream.h>
#include <unistd.h>

#include "kstars.h"
#include "ksutils.h"
#include "skymap.h"
#include "imageviewer.h"
#include "addlinkdialog.h"

#include <qglobal.h>
#if (QT_VERSION <= 299)
#include <kapp.h>
#else
#include <kapplication.h>
#include <qmemarray.h>
#endif

SkyMap::SkyMap(QWidget *parent, const char *name )
 : QWidget (parent,name), ClickedObject(0), FoundObject(0),  computeSkymap (true)
{
	ksw = (KStars*) kapp->mainWidget();

	pts = new QPointArray( 2000 );  // points for milkyway and horizon
	sp = new SkyPoint();            // needed by coordinate grid

//DEBUG
	dumpHorizon = false;
//END_DEBUG

	setDefaultMouseCursor();	// set the cross cursor
	kapp->config()->setGroup( "View" );
	ksw->data()->ZoomLevel = kapp->config()->readNumEntry( "ZoomLevel", 3 );
	if ( ksw->data()->ZoomLevel > NZOOM-1 ) ksw->data()->ZoomLevel = NZOOM-1;
	if ( ksw->data()->ZoomLevel < 0 )  ksw->data()->ZoomLevel = 0;
	if ( ksw->data()->ZoomLevel == NZOOM-1 ) ksw->actionCollection()->action("zoom_in")->setEnabled( false );
	if ( ksw->data()->ZoomLevel == 0  ) ksw->actionCollection()->action("zoom_out")->setEnabled( false );

	// load the pixmaps of stars
	starpix = new StarPixmap (ksw->options()->starColorMode, ksw->options()->starColorIntensity);

  //initialize pixelScale array, will be indexed by ZoomLevel
	for ( unsigned int i=0; i<NZOOM; ++i ) {
		pixelScale[i] = int( 256.*pow(sqrt(2.0),i) );
		Range[i] = 75.0*256.0/pixelScale[i];
	}

	setBackgroundColor( QColor( ksw->options()->colorSky ) );
	setBackgroundMode( QWidget::NoBackground );
	setFocusPolicy( QWidget::StrongFocus );
	setMinimumSize( 380, 250 );
	setSizePolicy( QSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding ) );

	setMouseTracking (true); //Generate MouseMove events!
	midMouseButtonDown = false;
	mouseButtonDown = false;
	slewing = false;
	clockSlewing = false;

	sky = new QPixmap();
	pmenu = new QPopupMenu();

	ClickedObject = NULL;
	FoundObject = NULL;

	connect( this, SIGNAL( destinationChanged() ), this, SLOT( slewFocus() ) );

	//Initialize Refraction correction lookup table arrays.  RefractCorr1 is for calculating
	//the apparent altitude from the true altitude, and RefractCorr2 is for the reverse.
	for ( unsigned int index = 0; index <184; ++index ) {
		double alt = -1.75 + index*0.5;  //start at -0.75 degrees to get midpoint value for each interval.

		RefractCorr1[index] = 1.02 / tan( PI()*( alt + 10.3/(alt + 5.11) )/180.0 ) / 60.0; //correction in degrees.
		RefractCorr2[index] = -1.0 / tan( PI()*( alt + 7.31/(alt + 4.4) )/180.0 ) / 60.0;
	}
}

SkyMap::~SkyMap() {
	delete starpix;
	delete pts;
	delete sp;
	delete sky;
	delete pmenu;

//delete any remaining object Image pointers
	for ( SkyObject *obj = ksw->data()->deepSkyList.first(); obj; obj = ksw->data()->deepSkyList.next() )
		if ( obj->image() ) obj->deleteImage();
}

void SkyMap::initPopupMenu( void ) {
	pmenu->clear();
	pmTitle = new QLabel( i18n( "nothing" ), pmenu );
	pmTitle->setAlignment( AlignCenter );
	QPalette pal( pmTitle->palette() );
	pal.setColor( QPalette::Normal, QColorGroup::Background, QColor( "White" ) );
	pal.setColor( QPalette::Normal, QColorGroup::Foreground, QColor( "Black" ) );
	pal.setColor( QPalette::Inactive, QColorGroup::Foreground, QColor( "Black" ) );
	pmTitle->setPalette( pal );
	pmTitle2 = new QLabel( QString::null, pmenu );
	pmTitle2->setAlignment( AlignCenter );
	pmTitle2->setPalette( pal );
	pmType = new QLabel( i18n( "no type" ), pmenu );
	pmType->setAlignment( AlignCenter );
	pmType->setPalette( pal );
	pmenu->insertItem( pmTitle );
	pmenu->insertItem( pmTitle2 );
	pmenu->insertItem( pmType );
	pmRiseTime = new QLabel( i18n( "Rise Time: 00:00" ), pmenu );
	pmRiseTime->setAlignment( AlignCenter );
	pmRiseTime->setPalette( pal );
	QFont rsFont = pmRiseTime->font();
	rsFont.setPointSize( rsFont.pointSize() - 2 );
	pmRiseTime->setFont( rsFont );
	pmSetTime = new QLabel( i18n( "Set Time: 00:00" ), pmenu );
	pmSetTime->setAlignment( AlignCenter );
	pmSetTime->setPalette( pal );
	pmSetTime->setFont( rsFont );
	pmTransitTime = new QLabel( i18n( "Transit Time: 00:00" ), pmenu );
	pmTransitTime->setAlignment( AlignCenter );
	pmTransitTime->setPalette( pal );
	pmTransitTime->setFont( rsFont );
	pmenu->insertSeparator();
	pmenu->insertItem( pmRiseTime );
	pmenu->insertItem( pmTransitTime );
	pmenu->insertItem( pmSetTime );
	pmenu->insertSeparator();
	pmenu->insertItem( i18n( "Center and Track" ), this, SLOT( slotCenter() ) );
	pmenu->insertSeparator();
}

void SkyMap::setGeometry( int x, int y, int w, int h ) {
	QWidget::setGeometry( x, y, w, h );
	sky->resize( w, h );
}

void SkyMap::setGeometry( const QRect &r ) {
	QWidget::setGeometry( r );
	sky->resize( r.width(), r.height() );
}

void SkyMap::slotCenter( void ) {
//If the requested object is below the opaque horizon, issue a warning message
//(unless user is already pointed below the horizon)
	clickedPoint()->EquatorialToHorizontal( ksw->data()->LSTh, ksw->geo()->lat() );

	if ( ksw->options()->useAltAz && ksw->options()->drawGround &&
			focus()->alt().Degrees() > -1.0 && clickedPoint()->alt().Degrees() < -1.0 ) {
		QString caption = i18n( "Requested position below horizon" );
		QString message = i18n( "The requested position is below the horizon.\nWould you like to go there anyway?" );

		if ( KMessageBox::warningYesNo( 0, message, caption )==KMessageBox::No ) {
			setClickedObject( NULL );
			setFoundObject( NULL );
			ksw->options()->isTracking = false;
			return;
		}
	}

//set FoundObject before slewing.  Otherwise, KStarsData::updateTime() can reset
//destination to previous object...
  setFoundObject( ClickedObject );

//update the destination to the selected coordinates
	if ( ksw->options()->useAltAz ) { //correct for atmospheric refraction if using horizontal coords
		setDestinationAltAz( refract( clickedPoint()->alt(), true ).Degrees(), clickedPoint()->az().Degrees() );
	} else {
		setDestination( clickedPoint() );
	}

	destination()->EquatorialToHorizontal( ksw->data()->LSTh, ksw->geo()->lat() );

	//display coordinates in statusBar
	QString sRA, sDec, s;
	char dsgn = '+';

	if ( clickedPoint()->dec().Degrees() < 0 ) dsgn = '-';
	int dd = abs( clickedPoint()->dec().degree() );
	int dm = abs( clickedPoint()->dec().getArcMin() );
	int ds = abs( clickedPoint()->dec().getArcSec() );

	sRA = sRA.sprintf( "%02d:%02d:%02d", clickedPoint()->ra().hour(), clickedPoint()->ra().minute(), clickedPoint()->ra().second() );
	sDec = sDec.sprintf( "%c%02d:%02d:%02d", dsgn, dd, dm, ds );
	s = sRA + ",  " + sDec;
	ksw->statusBar()->changeItem( s, 1 );

	ksw->showFocusCoords(); //update infoPanel
//	Update();	// must be new computed
}

void SkyMap::slotDSS( void ) {
	QString URLprefix( "http://archive.stsci.edu/cgi-bin/dss_search?v=1" );
	QString URLsuffix( "&e=J2000&h=15.0&w=15.0&f=gif&c=none&fov=NONE" );
	QString RAString, DecString;
	char decsgn;
	RAString = RAString.sprintf( "&r=%02d+%02d+%02d", clickedPoint()->ra().hour(),
																								 clickedPoint()->ra().minute(),
																								 clickedPoint()->ra().second() );
	decsgn = '+';
	if (clickedPoint()->dec().Degrees() < 0.0) decsgn = '-';
	int dd = abs( clickedPoint()->dec().degree() );
	int dm = abs( clickedPoint()->dec().getArcMin() );
	int ds = abs( clickedPoint()->dec().getArcSec() );

	DecString = DecString.sprintf( "&d=%c%02d+%02d+%02d", decsgn, dd, dm, ds );

	//concat all the segments into the kview command line:
	KURL url (URLprefix + RAString + DecString + URLsuffix);
	new ImageViewer (&url, this);
}

void SkyMap::slotDSS2( void ) {
	QString URLprefix( "http://archive.stsci.edu/cgi-bin/dss_search?v=2r" );
	QString URLsuffix( "&e=J2000&h=15.0&w=15.0&f=gif&c=none&fov=NONE" );
	QString RAString, DecString;
	char decsgn;
	RAString = RAString.sprintf( "&r=%02d+%02d+%02d", clickedPoint()->ra().hour(),
																								 clickedPoint()->ra().minute(),
																								 clickedPoint()->ra().second() );
	decsgn = '+';
	if (clickedPoint()->dec().Degrees() < 0.0) decsgn = '-';
	int dd = abs( clickedPoint()->dec().degree() );
	int dm = abs( clickedPoint()->dec().getArcMin() );
	int ds = abs( clickedPoint()->dec().getArcSec() );

	DecString = DecString.sprintf( "&d=%c%02d+%02d+%02d", decsgn, dd, dm, ds );

	//concat all the segments into the kview command line:
	KURL url (URLprefix + RAString + DecString + URLsuffix);
	new ImageViewer (&url, this);
}

void SkyMap::slotInfo( int id ) {
	QStringList::Iterator it = clickedObject()->InfoList.at(id-200);
	QString sURL = (*it);
	KURL url ( sURL );
	if (!url.isEmpty())
		kapp->invokeBrowser(sURL);
}

void SkyMap::slotImage( int id ) {
	QStringList::Iterator it = clickedObject()->ImageList.at(id-100);
  QString sURL = (*it);
	KURL url ( sURL );
	if (!url.isEmpty())
		new ImageViewer (&url, this);
}

void SkyMap::slotClockSlewing() {
//If the current timescale exceeds slewTimeScale, set clockSlewing=true, and stop the clock.
	if ( fabs( ksw->getClock()->scale() ) > ksw->options()->slewTimeScale ) {
		if ( ! clockSlewing ) {
			clockSlewing = true;
			ksw->getClock()->setManualMode( true );

			ksw->updateTime();
		}
	} else {
		if ( clockSlewing ) {
			clockSlewing = false;
			ksw->getClock()->setManualMode( false );

			ksw->updateTime();
		}
	}
}

void SkyMap::setFocusAltAz(double alt, double az) {
	focus()->setAlt(alt);
	focus()->setAz(az);
	focus()->HorizontalToEquatorial( ksw->data()->LSTh, ksw->geo()->lat() );
	slewing = false;
	oldfocus()->set( focus()->ra(), focus()->dec() );
	oldfocus()->setAz( focus()->az() );
	oldfocus()->setAlt( focus()->alt() );

	double dHA = ksw->data()->LSTh.Hours() - focus()->ra().Hours();
	while ( dHA < 0.0 ) dHA += 24.0;
	ksw->data()->HourAngle.setH( dHA );

	if ( slewing ) {
		if ( ksw->options()->isTracking ) {
			setClickedObject( NULL );
			setFoundObject( NULL );//no longer tracking foundObject
			ksw->options()->isTracking = false;
			ksw->actionCollection()->action("track_object")->setIconSet( BarIcon( "decrypted" ) );
		}
		ksw->showFocusCoords();
	}

	Update(); //need a total update, or slewing with the arrow keys doesn't work.
}

void SkyMap::setDestination( SkyPoint *p ) {
	Destination.set( p->ra(), p->dec() );
	destination()->EquatorialToHorizontal( ksw->data()->LSTh, ksw->geo()->lat() );
	emit destinationChanged();
}

void SkyMap::setDestinationAltAz(double alt, double az) {
	destination()->setAlt(alt);
	destination()->setAz(az);
	destination()->HorizontalToEquatorial( ksw->data()->LSTh, ksw->geo()->lat() );
	emit destinationChanged();
}

void SkyMap::slewFocus( void ) {
	double dX, dY, fX, fY, r;
	double step = 1.0;
	SkyPoint newFocus;

//Don't slew if the mouse button is pressed
//Also, no animated slews if the Manual Clock is active
	if ( !mouseButtonDown ) {
		if ( ksw->options()->useAnimatedSlewing && !( ksw->getClock()->isManualMode() && ksw->getClock()->isActive() ) ) {
			if ( ksw->options()->useAltAz ) {
				dX = destination()->az().Degrees() - focus()->az().Degrees();
				dY = destination()->alt().Degrees() - focus()->alt().Degrees();
			} else {
				dX = destination()->ra().Degrees() - focus()->ra().Degrees();
				dY = destination()->dec().Degrees() - focus()->dec().Degrees();
			}

			//switch directions to go the short way around the celestial sphere, if necessary.
			if ( dX < -180.0 ) dX = 360.0 + dX;
			else if ( dX > 180.0 ) dX = -360.0 + dX;

			r = sqrt( dX*dX + dY*dY );

			while ( r > step ) {
				fX = dX / r;
				fY = dY / r;
		
				if ( ksw->options()->useAltAz ) {
					focus()->setAlt( focus()->alt().Degrees() + fY*step );
					focus()->setAz( focus()->az().Degrees() + fX*step );
					focus()->HorizontalToEquatorial( ksw->data()->LSTh, ksw->geo()->lat() );
				} else {
					fX = fX/15.; //convert RA degrees to hours
					newFocus.set( focus()->ra().Hours() + fX*step, focus()->dec().Degrees() + fY*step );
					setFocus( &newFocus );
					focus()->EquatorialToHorizontal( ksw->data()->LSTh, ksw->geo()->lat() );
				}
	
				slewing = true;
				Update();
				kapp->processEvents(10); //keep up with other stuff
	
				if ( ksw->options()->useAltAz ) {
					dX = destination()->az().Degrees() - focus()->az().Degrees();
					dY = destination()->alt().Degrees() - focus()->alt().Degrees();
				} else {
					dX = destination()->ra().Degrees() - focus()->ra().Degrees();
					dY = destination()->dec().Degrees() - focus()->dec().Degrees();
				}
		
				//switch directions to go the short way around the celestial sphere, if necessary.
				if ( dX < -180.0 ) dX = 360.0 + dX;
				else if ( dX > 180.0 ) dX = -360.0 + dX;
		
				r = sqrt( dX*dX + dY*dY );
			}
		}

		//Either useAnimatedSlewing==false, or we have slewed, and are within one step of destination
		//set focus=destination.
		//Also, now that the focus has re-centered, engage tracking.
		setFocus( destination() );
		focus()->EquatorialToHorizontal( ksw->data()->LSTh, ksw->geo()->lat() );

		ksw->setHourAngle();
		slewing = false;

		if ( foundObject() != NULL ) { //set tracking to true
			ksw->options()->isTracking = true;
			ksw->actionCollection()->action("track_object")->setIconSet( BarIcon( "encrypted" ) );
		} else {
			ksw->options()->isTracking = false;
			ksw->actionCollection()->action("track_object")->setIconSet( BarIcon( "decrypted" ) );
		}

		Update();
	}
}

void SkyMap::invokeKey( int key ) {
	QKeyEvent *e = new QKeyEvent( QEvent::KeyPress, key, 0, 0 );
	keyPressEvent( e );
	delete e;
}

int SkyMap::findPA( SkyObject *o, int x, int y ) {
//	//no need for position angle for stars or open clusters
//	if ( o->type() == 0 || o->type() == 1 || o->type() == 3 ) return 0;

	//Find position angle of North using a test point displaced to the north
	//displace by 100/pixelScale radians (so distance is always 100 pixels)
	//this is 5730/pixelScale degrees
	double newDec = o->dec().Degrees() + 5730.0/pixelScale[ ksw->data()->ZoomLevel ];
	if ( newDec > 90.0 ) newDec = 90.0;
	SkyPoint test( o->ra().Hours(), newDec );
	if ( ksw->options()->useAltAz ) test.EquatorialToHorizontal( ksw->LSTh(), ksw->geo()->lat() );
	QPoint t = getXY( &test, ksw->options()->useAltAz, ksw->options()->useRefraction );
	double dx = double( x - t.x() );  //backwards to get counterclockwise angle
	double dy = double( t.y() - y );
	double north;
	if ( dy ) {
		north = atan( dx/dy )*180.0/PI();
	} else {
		north = 90.0;
		if ( dx > 0 ) north = -90.0;
	}

	int pa( 90 + int( north ) - o->pa() );
	return pa;
}

void SkyMap::drawSymbol( QPainter &psky, int type, int x, int y, int size, double e, int pa, QChar color ) {
	int dx1 = -size/2;
	int dx2 =  size/2;
	int dy1 = int( -e*size/2 );
	int dy2 = int( e*size/2 );
	int x1 = x + dx1;
	int x2 = x + dx2;
	int y1 = y + dy1;
	int y2 = y + dy2;

	int dxa = -size/4;
	int dxb =  size/4;
	int dya = int( -e*size/4 );
	int dyb = int( e*size/4 );
	int xa = x + dxa;
	int xb = x + dxb;
	int ya = y + dya;
	int yb = y + dyb;

	int psize = 2;

	QPixmap *star;

	switch (type) {
		case 0: //star
			//This line should only execute for KDE 3...the starpix images look bad for size==2.
			if ( QT_VERSION >=300 && size==2 ) size = 1;

			star = starpix->getPixmap (&color, size);
			bitBlt ((QPaintDevice *) sky, xa-star->width()/2, ya-star->height()/2, star);
			break;
		case 1: //catalog star
			//Some NGC/IC objects are stars...changed their type to 1 (was double star)
			if (size<2) size = 2;
			psky.drawEllipse( x1, y1, size/2, size/2 );
			break;
		case 2: //Planet
			break;
		case 3: //Open cluster
			psky.setBrush( psky.pen().color() );
			if ( size > 50 )  psize = 4;
			if ( size > 100 ) psize = 8;
			psky.drawEllipse( xa, y1, psize, psize ); // draw circle of points
			psky.drawEllipse( xb, y1, psize, psize );
			psky.drawEllipse( xa, y2, psize, psize );
			psky.drawEllipse( xb, y2, psize, psize );
			psky.drawEllipse( x1, ya, psize, psize );
			psky.drawEllipse( x1, yb, psize, psize );
			psky.drawEllipse( x2, ya, psize, psize );
			psky.drawEllipse( x2, yb, psize, psize );
			psky.setBrush( QColor( ksw->options()->colorSky ) );
			break;
		case 4: //Globular Cluster
			if (size<2) size = 2;
			psky.translate( x, y );
			psky.rotate( double( pa ) );  //rotate the coordinate system
			psky.drawEllipse( dx1, dy1, size, int( e*size ) );
			psky.moveTo( 0, dy1 );
			psky.lineTo( 0, dy2 );
			psky.moveTo( dx1, 0 );
			psky.lineTo( dx2, 0 );
			psky.resetXForm(); //reset coordinate system
			break;
		case 5: //Gaseous Nebula
			if (size <2) size = 2;
			psky.translate( x, y );
			psky.rotate( double( pa ) );  //rotate the coordinate system
			psky.drawLine( dx1, dy1, dx2, dy1 );
			psky.drawLine( dx2, dy1, dx2, dy2 );
			psky.drawLine( dx2, dy2, dx1, dy2 );
			psky.drawLine( dx1, dy2, dx1, dy1 );
			psky.resetXForm(); //reset coordinate system
			break;
		case 6: //Planetary Nebula
			if (size<2) size = 2;
			psky.translate( x, y );
			psky.rotate( double( pa ) );  //rotate the coordinate system
			psky.drawEllipse( dx1, dy1, size, int( e*size ) );
			psky.moveTo( 0, dy1 );
			psky.lineTo( 0, dy1 - int( e*size/2 ) );
			psky.moveTo( 0, dy2 );
			psky.lineTo( 0, dy2 + int( e*size/2 ) );
			psky.moveTo( dx1, 0 );
			psky.lineTo( dx1 - size/2, 0 );
			psky.moveTo( dx2, 0 );
			psky.lineTo( dx2 + size/2, 0 );
			psky.resetXForm(); //reset coordinate system
			break;
		case 7: //Supernova remnant
			if (size<2) size = 2;
			psky.translate( x, y );
			psky.rotate( double( pa ) );  //rotate the coordinate system
			psky.moveTo( 0, dy1 );
			psky.lineTo( dx2, 0 );
			psky.lineTo( 0, dy2 );
			psky.lineTo( dx1, 0 );
			psky.lineTo( 0, dy1 );
			psky.resetXForm(); //reset coordinate system
			break;
		case 8: //Galaxy
			if ( size <1 && ksw->data()->ZoomLevel > 8 ) size = 3; //force ellipse above zoomlevel 8
			if ( size <1 && ksw->data()->ZoomLevel > 5 ) size = 1; //force points above zoomlevel 5
			if ( size>2 ) {
				psky.translate( x, y );
				psky.rotate( double( pa ) );  //rotate the coordinate system
				psky.drawEllipse( dx1, dy1, size, int( e*size ) );
				psky.resetXForm(); //reset coordinate system
			} else if ( size>0 ) {
				psky.drawPoint( x, y );
			}
			break;
	}
}
//---------------------------------------------------------------------------

QPoint SkyMap::getXY( SkyPoint *o, bool Horiz, bool doRefraction ) {
	QPoint p;
	dms X, Y, X0, dX;
	double sindX, cosdX, sinY, cosY, sinY0, cosY0;

	if ( Horiz ) {
		X0 = focus()->az();

		X = o->az();
		if ( doRefraction ) Y = refract( o->alt(), true ); //account for atmospheric refraction
		else Y = o->alt();

		if ( X0.Degrees() > 270.0 && X.Degrees() < 90.0 ) {
			dX.setD( 360.0 + X0.Degrees() - X.Degrees() );
		} else {
			dX.setD( X0.Degrees() - X.Degrees() );
		}

		focus()->alt().SinCos( sinY0, cosY0 );

  } else {
		if (focus()->ra().Hours() > 18.0 && o->ra().Hours() < 6.0) {
			dX.setD( o->ra().Degrees() + 360.0 - focus()->ra().Degrees() );
		} else {
			dX.setD( o->ra().Degrees() - focus()->ra().Degrees() );
	  }
    Y = o->dec();
		focus()->dec().SinCos( sinY0, cosY0 );
  }

	//Convert dX, Y coords to screen pixel coords.
	dX.SinCos( sindX, cosdX );
	Y.SinCos( sinY, cosY );

	double c = sinY0*sinY + cosY0*cosY*cosdX;

	if ( c < 0.0 ) { //Object is on "back side" of the celestial sphere; don't plot it.
		p.setX( -10000000 );
		p.setY( -10000000 );
		return p;
	}

	double k = sqrt( 2.0/( 1 + c ) );

	p.setX( int( 0.5*width()  - pixelScale[ ksw->data()->ZoomLevel ]*k*cosY*sindX ) );
	p.setY( int( 0.5*height() - pixelScale[ ksw->data()->ZoomLevel ]*k*( cosY0*sinY - sinY0*cosY*cosdX ) ) );

	return p;
}
//---------------------------------------------------------------------------

SkyPoint SkyMap::dXdYToRaDec( double dx, double dy, bool useAltAz, dms LSTh, dms lat, bool doRefract ) {
	//Determine RA and Dec of a point, given (dx, dy): it's pixel
	//coordinates in the SkyMap with the center of the map as the origin.

	SkyPoint result;
	double sinDec, cosDec, sinDec0, cosDec0, sinc, cosc, sinlat, coslat;
	double xx, yy;

	double r  = sqrt( dx*dx + dy*dy );
	dms centerAngle;
	centerAngle.setRadians( 2.0*asin(0.5*r) );

	focus()->dec().SinCos( sinDec0, cosDec0 );
	centerAngle.SinCos( sinc, cosc );

	if ( useAltAz ) {
		dms HA;
		dms Dec, alt, az, alt0, az0;
		double A;
		double sinAlt, cosAlt, sinAlt0, cosAlt0, sinAz, cosAz;
//		double HA0 = LSTh - focus.ra();
		az0 = focus()->az();
		alt0 = focus()->alt();
		alt0.SinCos( sinAlt0, cosAlt0 );

		dx = -dx; //Flip East-west (Az goes in opposite direction of RA)
		yy = dx*sinc;
		xx = r*cosAlt0*cosc - dy*sinAlt0*sinc;

		A = atan( yy/xx );
		//resolve ambiguity of atan():
		if ( xx<0 ) A = A + PI();
//		if ( xx>0 && yy<0 ) A = A + 2.0*PI();

		dms deltaAz;
		deltaAz.setRadians( A );
		az = focus()->az().Degrees() + deltaAz.Degrees();
		alt.setRadians( asin( cosc*sinAlt0 + ( dy*sinc*cosAlt0 )/r ) );

		if ( doRefract ) alt.setD( refract( alt, false ).Degrees() );  //find true altitude from apparent altitude

		az.SinCos( sinAz, cosAz );
		alt.SinCos( sinAlt, cosAlt );
		lat.SinCos( sinlat, coslat );

		Dec.setRadians( asin( sinAlt*sinlat + cosAlt*coslat*cosAz ) );
		Dec.SinCos( sinDec, cosDec );

		HA.setRadians( acos( ( sinAlt - sinlat*sinDec )/( coslat*cosDec ) ) );
		if ( sinAz > 0.0 ) HA.setH( 24.0 - HA.Hours() );

		result.setRA( LSTh.Hours() - HA.Hours() );
		result.setRA( result.ra().reduce() );
		result.setDec( Dec.Degrees() );

		return result;

  } else {
		yy = dx*sinc;
		xx = r*cosDec0*cosc - dy*sinDec0*sinc;

		double RARad = ( atan( yy / xx ) );
		//resolve ambiguity of atan():
		if ( xx<0 ) RARad = RARad + PI();
//		if ( xx>0 && yy<0 ) RARad = RARad + 2.0*PI();

		dms deltaRA, Dec;
		deltaRA.setRadians( RARad );
		Dec.setRadians( asin( cosc*sinDec0 + (dy*sinc*cosDec0)/r ) );

		result.setRA( focus()->ra().Hours() + deltaRA.Hours() );
		result.setRA( result.ra().reduce() );
		result.setDec( Dec.Degrees() );

		return result;
	}
}

dms SkyMap::refract( dms alt, bool findApparent ) {
	int index = int( ( alt.Degrees() + 2.0 )*2. );  //RefractCorr arrays start at alt=-2.0 degrees.
	dms result;

	if ( alt.Degrees() <= -2.000 ) return alt;

	if ( findApparent ) {
		result.setD( alt.Degrees() + RefractCorr1[index] );
	} else {
		result.setD( alt.Degrees() + RefractCorr2[index] );
	}	

	return result;
}

//---------------------------------------------------------------------------


// force new compute of the skymap (used instead of update())
void SkyMap::Update()
{
	computeSkymap = true;
	update();
}

//Identical to Update(), except calls repaint() instead of update(), so
//paintEvent gets executed immediately instead of just adding it to the event
//queue.
void SkyMap::UpdateNow()
{
	computeSkymap = true;
	repaint();
}


float SkyMap::fov( void ) {
	return Range[ ksw->data()->ZoomLevel ]*width()/600.;
}

bool SkyMap::checkVisibility( SkyPoint *p, float FOV, bool useAltAz, bool isPoleVisible ) {
	double dX, dY, XMax;

	if ( useAltAz ) {
		dY = fabs( p->alt().Degrees() - focus()->alt().Degrees() );
	} else {
		dY = fabs( p->dec().Degrees() - focus()->dec().Degrees() );
	}
	if ( dY > FOV ) return false;
	if ( isPoleVisible ) return true;

	if ( useAltAz ) {
		dX = fabs( p->az().Degrees() - focus()->az().Degrees() );
		XMax = 1.2*FOV/cos( focus()->alt().radians() );
	} else {
		dX = fabs( p->ra().Degrees() - focus()->ra().Degrees() );
		XMax = 1.2*FOV/cos( focus()->dec().radians() );
	}
	if ( dX > 180.0 ) dX = 360.0 - dX; // take shorter distance around sky

	if ( dX < XMax ) {
		return true;
	} else {
		return false;
	}
}

bool SkyMap::unusablePoint (double dx, double dy)
{
	if (dx >= 1.41 || dx <= -1.41 || dy >= 1.41 || dy <= -1.41)
		return true;
	else
		return false;
}

void SkyMap::setDefaultMouseCursor()
{
	mouseMoveCursor = false;	// no mousemove cursor
	QPainter p;
	QPixmap cursorPix (32, 32); // size 32x32 (this size is compatible to all systems)
// the center of the pixmap
	int mx = cursorPix.	width() / 2;
	int my = cursorPix.	height() / 2;

	cursorPix.fill (white);  // white background
	p.begin (&cursorPix);
	p.setPen (QPen (black, 2));	// black lines
// 1. diagonal
	p.drawLine (mx - 2, my - 2, mx - 8, mx - 8);
	p.drawLine (mx + 2, my + 2, mx + 8, mx + 8);
// 2. diagonal
	p.drawLine (mx - 2, my + 2, mx - 8, mx + 8);
	p.drawLine (mx + 2, my - 2, mx + 8, mx - 8);
	p.end();

// create a mask to make parts of the pixmap invisible
	QBitmap mask (32, 32);
	mask.fill (color0);	// all is invisible

	p.begin (&mask);
// paint over the parts which should be visible
	p.setPen (QPen (color1, 3));
// 1. diagonal
	p.drawLine (mx - 2, my - 2, mx - 8, mx - 8);
	p.drawLine (mx + 2, my + 2, mx + 8, mx + 8);
// 2. diagonal
	p.drawLine (mx - 2, my + 2, mx - 8, mx + 8);
	p.drawLine (mx + 2, my - 2, mx + 8, mx - 8);
	p.end();

	cursorPix.setMask (mask);	// set the mask
	QCursor cursor (cursorPix);
	setCursor (cursor);
}

void SkyMap::setMouseMoveCursor()
{
	if (mouseButtonDown)
	{
		setCursor (9);	// cursor shape defined in qt
		mouseMoveCursor = true;
	}
}

void SkyMap::addLink( void ) {
	AddLinkDialog adialog( this );
	QString entry;
  QFile file;

	if ( adialog.exec()==QDialog::Accepted ) {
		if ( adialog.isImageLink() ) {
			//Add link to object's ImageList, and descriptive text to its ImageTitle list
			clickedObject()->ImageList.append( adialog.url() );
			clickedObject()->ImageTitle.append( adialog.title() );

			//Also, update the user's custom image links database
			//check for user's image-links database.  If it doesn't exist, create it.
			file.setName( locateLocal( "appdata", "myimage_url.dat" ) ); //determine filename in local user KDE directory tree.

			if ( !file.open( IO_ReadWrite | IO_Append ) ) {
				QString message = i18n( "Custom image-links file could not be opened.\nLink cannot be recorded for future sessions." );		
				KMessageBox::sorry( 0, message, i18n( "Could not Open File" ) );
				return;
			} else {
				entry = clickedObject()->name() + ":" + adialog.title() + ":" + adialog.url();
				QTextStream stream( &file );
				stream << entry << endl;
				file.close();
      }
		} else {
			clickedObject()->InfoList.append( adialog.url() );
			clickedObject()->InfoTitle.append( adialog.title() );

			//check for user's image-links database.  If it doesn't exist, create it.
			file.setName( locateLocal( "appdata", "myinfo_url.dat" ) ); //determine filename in local user KDE directory tree.

			if ( !file.open( IO_ReadWrite | IO_Append ) ) {
				QString message = i18n( "Custom information-links file could not be opened.\nLink cannot be recorded for future sessions." );						KMessageBox::sorry( 0, message, i18n( "Could not Open File" ) );
				return;
			} else {
				entry = clickedObject()->name() + ":" + adialog.title() + ":" + adialog.url();
				QTextStream stream( &file );
				stream << entry;
				file.close();
      }
		}
	}
}

void SkyMap::setRiseSetLabels( void ) {
	QTime rtime = clickedObject()->riseTime( ksw->data()->CurrentDate, ksw->geo() );
	QString rt, rt2;
	if ( rtime.isValid() ) {
		int min = rtime.minute();
		if ( rtime.second() >=30 ) ++min;
		rt2.sprintf( "%02d:%02d", rtime.hour(), min );
		rt = i18n( "Rise Time: " ) + rt2;
	} else if ( clickedObject()->alt().Degrees() > 0 ) {
		rt = i18n( "No Rise Time: Circumpolar" );
	} else {
		rt = i18n( "No Rise Time: Never rises" );
	}

	QTime stime = clickedObject()->setTime( ksw->data()->CurrentDate, ksw->geo() );
	QString st, st2;
	if ( stime.isValid() ) {
		int min = stime.minute();
		if ( stime.second() >=30 ) ++min;
		st2.sprintf( "%02d:%02d", stime.hour(), min );
		st = i18n( "Set Time: " ) + st2;
	} else if ( clickedObject()->alt().Degrees() > 0 ) {
		st = i18n( "No Set Time: Circumpolar" );
	} else {
		st = i18n( "No Set Time: Never rises" );
	}

	QTime ttime = clickedObject()->transitTime( ksw->data()->CurrentDate, ksw->geo() );
	QString tt, tt2;
	if ( ttime.isValid() ) {
		int min = ttime.minute();
		if ( ttime.second() >=30 ) ++min;
		tt2.sprintf( "%02d:%02d", ttime.hour(), min );
		tt = i18n( "Transit Time: " ) + tt2;
	} else if ( clickedObject()->alt().Degrees() > 0 ) {
		tt = i18n( "No Transit Time: Circumpolar" );
	} else {
		tt = i18n( "No Transit Time: Never rises" );
	}

	pmRiseTime->setText( rt );
	pmSetTime->setText( st );
	pmTransitTime->setText( tt ) ;
}

int SkyMap::getPixelScale( void ) {
	return pixelScale[ ksw->data()->ZoomLevel ];
}

bool SkyMap::setColors( QString filename ) {
//	QPixmap *temp = new QPixmap( 30, 20 );
	QFile file;
	int i=0;
	bool colonLineFound = false;

	if ( !KSUtils::openDataFile( file, filename ) ) {
		file.setName( locateLocal( "appdata", filename ) ); //try filename in local user KDE directory tree.
		if ( !file.open( IO_ReadOnly ) ) {
			return false;
    }
	}

	QTextStream stream( &file );
	QString line;

	//first line is the star-color mode
  line = stream.readLine();
	int newmode = line.left(1).toInt();
	ksw->options()->starColorMode = newmode;
	if ( starColorMode() != newmode )
		setStarColorMode( newmode );

//More flexible method for reading in color values.  Any order is acceptable, and
//missing entries are ignored.
	while ( !stream.eof() ) {
		line = stream.readLine();

		if ( line.contains(':')==1 ) { //the new color preset format contains a ":" in each line, followed by the name of the color
     colonLineFound = true;

			if ( i > 0 ) return false; //we read at least one line without a colon...file is corrupted.

			if ( line.mid( line.find(':')+1 ).contains( "colorSky" ) ) {
				ksw->options()->colorSky = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorMess" ) ) {
				ksw->options()->colorMess = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorNGC" ) ) {
				ksw->options()->colorNGC = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorIC" ) ) {
				ksw->options()->colorIC = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorHST" ) ) {
				ksw->options()->colorHST = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorSName" ) ) {
				ksw->options()->colorSName = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorPName" ) ) {
				ksw->options()->colorPName = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorCName" ) ) {
				ksw->options()->colorCName = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorCLine" ) ) {
				ksw->options()->colorCLine = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorMW" ) ) {
				ksw->options()->colorMW = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorEq" ) ) {
				ksw->options()->colorEq = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorEcl" ) ) {
				ksw->options()->colorEcl = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorHorz" ) ) {
				ksw->options()->colorHorz = line.left( line.find(':')-1 );
			} else if ( line.mid( line.find(':')+1 ).contains( "colorGrid" ) ) {
				ksw->options()->colorGrid = line.left( line.find(':')-1 );
			}

		} else { // no ':' seen in the line, so we must assume the old format

			if ( colonLineFound ) return false; //a previous line had a colon, this line doesn't.  File is corrupted.

			ksw->options()->colorSky = line.left( 7 );
			ksw->options()->colorMess = line.left( 7 );
			ksw->options()->colorNGC = line.left( 7 );
			ksw->options()->colorIC = line.left( 7 );
			ksw->options()->colorHST = line.left( 7 );
			ksw->options()->colorSName = line.left( 7 );
			ksw->options()->colorPName = line.left( 7 );
			ksw->options()->colorCName = line.left( 7 );
			ksw->options()->colorCLine = line.left( 7 );
			ksw->options()->colorMW = line.left( 7 );
			ksw->options()->colorEq = line.left( 7 );
			ksw->options()->colorEcl = line.left( 7 );
			ksw->options()->colorHorz = line.left( 7 );
			ksw->options()->colorGrid = line.left( 7 );
		}
	}

	Update();
	return true;
}

#include "skymap.moc"
