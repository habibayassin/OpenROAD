/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2020, OpenROAD
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "staGui.h"

#include <string>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "openroad/OpenRoad.hh"
#include "sta/Corner.hh"
#include "sta/Graph.hh"
#include "sta/Liberty.hh"
#include "sta/Network.hh"
#include "sta/PathEnd.hh"
#include "sta/PathExpanded.hh"
#include "sta/PathRef.hh"
#include "sta/PatternMatch.hh"
#include "sta/PortDirection.hh"
#include "sta/Sdc.hh"
#include "sta/Search.hh"
#include "sta/Sta.hh"

namespace gui {
// helper functions
float getRequiredTime(sta::dbSta* staRoot,
                      sta::Pin* term,
                      bool is_rise,
                      sta::PathAnalysisPt* path_ap)
{
  auto vert = staRoot->getDbNetwork()->graph()->pinLoadVertex(term);
  auto req = staRoot->vertexRequired(
      vert, is_rise ? sta::RiseFall::rise() : sta::RiseFall::fall(), path_ap);
  if (sta::delayInf(req)) {
    return 0;
  }
  return req;
}

TimingPathsModel::TimingPathsModel() : openroad_(ord::OpenRoad::openRoad())
{
  populateModel();
}

int TimingPathsModel::rowCount(const QModelIndex& parent) const
{
  return timing_paths_.size();
}

int TimingPathsModel::columnCount(const QModelIndex& parent) const
{
  return TimingPathsModel::_path_columns.size();
}

QVariant TimingPathsModel::data(const QModelIndex& index, int role) const
{
  return QVariant();
}

QVariant TimingPathsModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const
{
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
    return QString(TimingPathsModel::_path_columns[section].c_str());
  return QVariant();
}

// Get the network for commands.
void TimingPathsModel::findInstances(std::string pattern,
                                     std::vector<odb::dbInst*>& insts)
{
  if (!openroad_)
    return;
  std::vector<sta::Instance*> staInsts = findInstancesNetwork(pattern);
  for (auto staInst : staInsts) {
    insts.push_back(openroad_->getSta()->getDbNetwork()->staToDb(staInst));
  }
  return;
}

void TimingPathsModel::findNets(std::string pattern,
                                std::vector<odb::dbNet*>& nets)
{
  if (!openroad_)
    return;
  std::vector<sta::Net*> staNets = findNetsNetwork(pattern);
  for (auto staNet : staNets) {
    nets.push_back(openroad_->getSta()->getDbNetwork()->staToDb(staNet));
  }
  return;
}

void TimingPathsModel::findPins(std::string pattern,
                                std::vector<odb::dbObject*>& pins)
{
  if (!openroad_)
    return;
  std::vector<sta::Pin*> staPins = findPinsNetwork(pattern);
  for (auto staPin : staPins) {
    odb::dbITerm* term;
    odb::dbBTerm* port;
    openroad_->getSta()->getDbNetwork()->staToDb(staPin, term, port);
    odb::dbObject* pinObject
        = term ? (odb::dbObject*) term : (odb::dbObject*) port;
    pins.push_back(pinObject);
  }
  return;
}

std::vector<sta::Instance*> TimingPathsModel::findInstancesNetwork(
    std::string pattern)
{
  sta::InstanceSeq all_insts;
  sta::PatternMatch staPattern(pattern.c_str());
  if (openroad_)
    openroad_->getSta()->getDbNetwork()->findInstancesMatching(
        openroad_->getSta()->getDbNetwork()->topInstance(),
        &staPattern,
        &all_insts);
  return static_cast<std::vector<sta::Instance*>>(all_insts);
}

std::vector<sta::Net*> TimingPathsModel::findNetsNetwork(std::string pattern)
{
  sta::NetSeq all_nets;
  sta::PatternMatch staPattern(pattern.c_str());
  if (openroad_)
    openroad_->getSta()->getDbNetwork()->findNetsMatching(
        openroad_->getSta()->getDbNetwork()->topInstance(),
        &staPattern,
        &all_nets);
  return static_cast<std::vector<sta::Net*>>(all_nets);
}

std::vector<sta::Pin*> TimingPathsModel::findPinsNetwork(std::string pattern)
{
  sta::PinSeq all_pins;
  sta::PatternMatch staPattern(pattern.c_str());
  if (openroad_)
    openroad_->getSta()->getDbNetwork()->findPinsMatching(
        openroad_->getSta()->getDbNetwork()->topInstance(),
        &staPattern,
        &all_pins);
  return static_cast<std::vector<sta::Pin*>>(all_pins);
}

void TimingPathsModel::populateModel()
{
  beginResetModel();
  populatePaths();
  endResetModel();
}

bool TimingPathsModel::populatePaths(bool get_max, int path_count)
{
  // On lines of DataBaseHandler
  sta::dbSta* sta_ = openroad_->getSta();
  sta_->ensureGraph();
  sta_->searchPreamble();

  sta::PathEndSeq* path_ends
      = sta_->search()->findPathEnds(  // from, thrus, to, unconstrained
          nullptr,
          nullptr,
          nullptr,
          false,
          // corner, min_max,
          sta_->findCorner("default"),
          get_max ? sta::MinMaxAll::max() : sta::MinMaxAll::min(),
          // group_count, endpoint_count, unique_pins
          path_count,
          path_count,
          true,
          -sta::INF,
          sta::INF,  // slack_min, slack_max,
          true,      // sort_by_slack
          nullptr,   // group_names
          // setup, hold, recovery, removal,
          true,
          true,
          true,
          true,
          // clk_gating_setup, clk_gating_hold
          true,
          true);

  bool first_path = true;
  for (auto& path_end : *path_ends) {
    sta::PathExpanded expanded(path_end->path(), sta_);

    TimingPath* path = new TimingPath();

    for (size_t i = 1; i < expanded.size(); i++) {
      auto ref = expanded.path(i);
      auto pin = ref->vertex(sta_)->pin();
      auto is_rising = ref->transition(sta_) == sta::RiseFall::rise();
      auto arrival = ref->arrival(sta_);
      auto path_ap = ref->pathAnalysisPt(sta_);
      auto path_required = !first_path ? 0 : ref->required(sta_);
      if (!path_required || sta::delayInf(path_required)) {
        path_required = getRequiredTime(sta_, pin, is_rising, path_ap);
      }
      auto slack = !first_path ? path_required - arrival : ref->slack(sta_);
      odb::dbITerm* term;
      odb::dbBTerm* port;
      openroad_->getSta()->getDbNetwork()->staToDb(pin, term, port);
      odb::dbObject* pinObject
          = term ? (odb::dbObject*) term : (odb::dbObject*) port;
      path->appendNode(
          TimingPathNode(pinObject, is_rising, arrival, path_required, slack));
      first_path = false;
    }
    timing_paths_.push_back(path);
  }

  delete path_ends;
  return true;
}

int TimingPathDetailModel::rowCount(const QModelIndex& parent) const
{
  return path_->levelsCount();
}

int TimingPathDetailModel::columnCount(const QModelIndex& parent) const
{
  return TimingPathDetailModel::_path_details_columns.size();
}

QVariant TimingPathDetailModel::data(const QModelIndex& index, int role) const
{
  return QVariant();
}

QVariant TimingPathDetailModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const
{
  if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
    return QString(
        TimingPathDetailModel::_path_details_columns[section].c_str());
  return QVariant();
}

void TimingPathDetailModel::populateModel(TimingPath* path)
{
  beginResetModel();
  path_ = path;
  endResetModel();
}
}  // namespace gui
