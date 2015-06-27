/***************************************************************************
                          skyguidemgr.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2015/05/06
    copyright            : (C) 2015 by Marcos Cardinot
    email                : mcardinot@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "skyguidemgr.h"

SkyGuideMgr::SkyGuideMgr()
{
    m_view = new QQuickView();
    QString qmlViewPath = "tools/skyguide/resources/skyguideview.qml";
    m_view->setSource(QStandardPaths::locate(QStandardPaths::DataLocation, qmlViewPath));
    m_view->show();

    m_container = QWidget::createWindowContainer(m_view);
    m_container->setMinimumWidth(m_view->width());
    m_container->setMaximumWidth(m_view->width());
    m_container->setFocusPolicy(Qt::TabFocus);

    m_dock = new QDockWidget();
    m_dock->setObjectName("Sky Guide");
    m_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dock->setWidget(m_container);
    m_dock->setMinimumWidth(m_view->width());
}

SkyGuideMgr::~SkyGuideMgr()
{
}

void SkyGuideMgr::loadSkyGuideObject(const QString& jsonPath)
{
    QFile jsonFile(jsonPath);
    if (!jsonFile.exists()) {
        qWarning() << "SkyGuideMgr: The JSON file does not exist!"
                   << QDir::toNativeSeparators(jsonPath);
    } else if (!jsonFile.open(QIODevice::ReadOnly)) {
        qWarning() << "SkyGuideMgr: Couldn't open the JSON file!"
                   << QDir::toNativeSeparators(jsonPath);
    } else {
        QJsonObject json(QJsonDocument::fromJson(jsonFile.readAll()).object());
        SkyGuideObject* s = new SkyGuideObject(json.toVariantMap());
        if (s->isValid()) {
            m_skyGuideObjects.append(s);
        } else {
            qWarning()  << "SkyGuideMgr: SkyGuide is invalid!"
                        << QDir::toNativeSeparators(jsonPath);
        }
    }
}
