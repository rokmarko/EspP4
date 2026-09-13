/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The scale, named for the ThorVG back end.
//
// The drawing itself lives in ScaleDraw.h / ScaleDraw.cpp and is written
// against the Painter concept; this header only pairs it with the back end
// this firmware draws through. The scale::tvg names are what VectorScene.cpp
// has always used and they still mean the same thing.

#include "PainterTvg.h"
#include "ScaleDraw.h"

namespace scale { namespace tvg {

	using Painter = ::scale::PainterTvg;
	using Scale	  = ::scale::Scale;

}} // namespace scale::tvg
