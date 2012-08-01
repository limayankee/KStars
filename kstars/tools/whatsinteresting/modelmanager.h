/***************************************************************************
                          modelmanager.h  -  K Desktop Planetarium
                             -------------------
    begin                : 2012/26/05
    copyright            : (C) 2012 by Samikshan Bairagya
    email                : samikshan@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

//#include <QStandardItemModel>
#include "skyobjlistmodel.h"
#include "kstarsdata.h"
#include "obsconditions.h"

class ModelManager
{
public:
    enum LIST_TYPE { BaseList, DeepSkyObjects, Planets, Stars, Galaxies, Constellations, Clusters, Nebulae };
    ModelManager(ObsConditions *obs);
    ~ModelManager();
    void updateModels();

    SkyObjListModel* returnModel ( LIST_TYPE type );
    SkyObjListModel* returnModel ( QString type );
    QStringList returnCatListModel ( LIST_TYPE Type );

private:
    ObsConditions *obsconditions;
    SkyObjListModel *planetsModel, *starsModel, *galModel, *conModel, *clustModel, *nebModel;
    QStringList baseCatList, planetaryList, deepSkyList;
    QHash < QString , QList < SkyObject * > > initobjects;
};