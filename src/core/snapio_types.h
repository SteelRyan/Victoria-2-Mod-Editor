// The fields of every core struct a store's picture holds (snapio.h). A field left out here is lost in the recovery
// file — tests/core/snapio_check.cpp packs and unpacks every store of the scripted sessions and compares by value.
#pragma once
#include "core/adjacency.h"
#include "core/countryhist.h"
#include "core/diplomacy.h"
#include "core/document.h"
#include "core/history.h"
#include "core/mapfiles.h"
#include "core/moddata.h"
#include "core/oob.h"
#include "core/positions.h"
#include "core/production.h"
#include "core/snapio.h"
#include "core/techdata.h"

namespace snapio {

SNAPIO_FIELDS(moddata::PopOrigin, x.file, x.span, x.size_span, x.line, x.type, x.culture, x.religion, x.size, x.extra)
SNAPIO_FIELDS(moddata::Pop, x.type, x.culture, x.religion, x.size, x.extra, x.o)
SNAPIO_FIELDS(moddata::Province, x.id, x.rgb, x.name, x.sea, x.region, x.owner, x.controller, x.cores, x.life_rating,
              x.trade_goods, x.history_file)
SNAPIO_FIELDS(history::FactorySrc, x.path, x.span, x.level_span, x.date, x.level)
SNAPIO_FIELDS(history::Factory, x.building, x.level, x.upgrade, x.src)
SNAPIO_FIELDS(history::ProvState, x.owner, x.controller, x.cores, x.life_rating, x.trade_goods, x.colonial, x.fort,
              x.naval_base, x.railroad, x.factories, x.flags, x.more)
SNAPIO_FIELDS(document::Picture, x.pops, x.regs, x.provs, x.lists)
SNAPIO_FIELDS(adjacency::Row, x.uid, x.a, x.b, x.type, x.through, x.data, x.comment)
SNAPIO_FIELDS(adjacency::Mark, x.uid, x.name, x.tiles)
SNAPIO_FIELDS(adjacency::AdjacencyFile::Snap, x.rows, x.marks, x.no_coast, x.next_uid)
SNAPIO_FIELDS(countryhist::Src, x.date, x.span)
SNAPIO_FIELDS(countryhist::NationState, x.scalars, x.cultures, x.reforms, x.techs, x.inventions, x.upper_house, x.src)
SNAPIO_FIELDS(countryhist::NewNation, x.name, x.adj, x.color, x.template_, x.file, x.ctext, x.htext, x.stored,
              x.imported)
SNAPIO_FIELDS(diplomacy::Record, x.id, x.kind, x.first, x.second, x.start, x.end, x.file, x.span)
SNAPIO_FIELDS(oob::Regiment, x.name, x.type, x.home)
SNAPIO_FIELDS(oob::Ship, x.name, x.type)
SNAPIO_FIELDS(oob::ArmyKey, x.name, x.location, x.regs, x.extra)
SNAPIO_FIELDS(oob::NavyKey, x.name, x.location, x.ships, x.extra)
SNAPIO_FIELDS(oob::Army, x.name, x.location, x.regiments, x.extra, x.src, x.orig)
SNAPIO_FIELDS(oob::Navy, x.name, x.location, x.ships, x.extra, x.src, x.orig)
SNAPIO_FIELDS(oob::Relation, x.value, x.level, x.influence_value, x.src, x.orig)
SNAPIO_FIELDS(oob::LeaderKey, x.fields, x.extra)
SNAPIO_FIELDS(oob::Leader, x.fields, x.extra, x.src, x.orig)
SNAPIO_FIELDS(oob::OOB, x.armies, x.navies, x.relations, x.leaders)
SNAPIO_FIELDS(production::ProdSnap, x.want, x.added, x.removed, x.new_buildings, x.cost_want, x.bld_want, x.need_want)
SNAPIO_FIELDS(techdata::Added, x.file, x.text, x.kind)
SNAPIO_FIELDS(techdata::TechSnap, x.want, x.added, x.order, x.removed)
SNAPIO_FIELDS(positions::Entry, x.pts, x.text_rotation, x.text_scale, x.naval_rot, x.extra, x.bp_extra, x.br_extra)

}  // namespace snapio
