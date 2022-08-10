// BSD 3-Clause License
//
// Copyright (c) 2020, MICL, DD-Lab, University of Michigan
// All rights reserved.
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

#pragma once

#include <map>
#include <unordered_set>

#include "odb/db.h"
#include "odb/dbWireGraph.h"
#include "utl/Logger.h"

namespace grt {
class GlobalRouter;
}

namespace ant {

using std::vector;

using odb::dbInst;
using odb::dbITerm;
using odb::dbMTerm;
using odb::dbNet;
using odb::dbSWire;
using odb::dbTechLayer;
using odb::dbWire;
using odb::dbWireGraph;

using utl::Logger;

struct PARinfo;
struct ARinfo;
struct AntennaModel;

struct Violation
{
  int routing_level;
  vector<odb::dbITerm*> gates;
  int diode_count_per_gate;
};

// A struct that contains a net as well as one of its routing levels.
// This struct is used to index the map, allowed_wire_length which is created 
// in the class below.
struct NetLayerPair
{
    dbNet* net;
    int routing_level;
 
    // constructor
    NetLayerPair(dbNet* net, int routing_level): net(net), routing_level(routing_level) {}
 
    // overload `<` operator to use this struct as a key for an std::map
    bool operator<(const NetLayerPair &ob) const {
        return net->getId() < ob.net->getId() ||  (!(ob.net->getId()  < net->getId()) && routing_level < ob.routing_level);
    }
};



class AntennaChecker
{
 public:
  AntennaChecker();
  ~AntennaChecker();

  void init(odb::dbDatabase* db,
            grt::GlobalRouter* global_router,
            utl::Logger* logger);

  // net_name nullptr -> check all nets
  int checkAntennas(const char *net_name,
                    bool verbose);
  int antennaViolationCount() const;

  void findMaxAllowedLength(const char* net_name, const char* layer);

  void findMaxWireLength();

  vector<Violation> getAntennaViolations(dbNet* net,
                                         odb::dbMTerm* diode_mterm);
  void initAntennaRules();

 private:
  bool haveRoutedNets();
  double dbuToMicrons(int value);

  dbWireGraph::Node* findSegmentRoot(dbWireGraph::Node* node_info,
                                     int wire_level);
  dbWireGraph::Node* findSegmentStart(dbWireGraph::Node* node);
  bool ifSegmentRoot(dbWireGraph::Node* node, int wire_level);

  void findWireBelowIterms(dbWireGraph::Node* node,
                           double &iterm_gate_area,
                           double &iterm_diff_area,
                           int wire_level,
                           std::set<dbITerm*>& iv,
                           std::set<dbWireGraph::Node*>& nv);
  std::pair<double, double> calculateWireArea(
      dbWireGraph::Node* node,
      int wire_level,
      std::set<dbWireGraph::Node*>& nv,
      std::set<dbWireGraph::Node*>& level_nodes);

  double getViaArea(dbWireGraph::Edge* edge);
  dbTechLayer* getViaLayer(dbWireGraph::Edge* edge);
  std::string getViaName(dbWireGraph::Edge* edge);
  double calculateViaArea(dbWireGraph::Node* node, int wire_level);
  dbWireGraph::Edge* findVia(dbWireGraph::Node* node, int wire_level);

  void findCarPath(dbWireGraph::Node* node,
                   int wire_level,
                   dbWireGraph::Node* goal,
                   vector<dbWireGraph::Node*>& current_path,
                   vector<dbWireGraph::Node*>& path_found);

  void calculateParInfo(PARinfo& PARtable);
  double getPwlFactor(odb::dbTechLayerAntennaRule::pwl_pair pwl_info,
                      double ref_val,
                      double def);

  vector<PARinfo> buildWireParTable(const vector<dbWireGraph::Node*> &wire_roots);
  vector<ARinfo> buildWireCarTable(const vector<PARinfo> &PARtable,
                                        const vector<PARinfo> &VIA_PARtable,
                                        const vector<dbWireGraph::Node*> &gate_iterms);
  vector<PARinfo> buildViaParTable(const vector<dbWireGraph::Node*> &wire_roots);
  vector<ARinfo> buildViaCarTable(const vector<PARinfo> &PARtable,
                                       const vector<PARinfo> &VIA_PARtable,
                                       const vector<dbWireGraph::Node*> &gate_iterms);

  vector<dbWireGraph::Node*> findWireRoots(dbWire* wire);
  void findWireRoots(dbWire* wire,
                     // Return values.
                     vector<dbWireGraph::Node*> &wire_roots,
                     vector<dbWireGraph::Node*> &gate_iterms);

  std::pair<bool, bool> checkWirePar(ARinfo AntennaRatio, dbNet* net,
                                     bool verbose,
                                     bool report);
  std::pair<bool, bool> checkWireCar(ARinfo AntennaRatio,
                                     bool par_checked,
                                     bool verbose,
                                     bool report);
  bool checkViaPar(ARinfo AntennaRatio, bool verbose, bool report);
  bool checkViaCar(ARinfo AntennaRatio, bool verbose, bool report);

  void checkNet(dbNet* net,
                bool report_if_no_violation,
                bool verbose,
                // Return values.
                int &net_violation_count,
                int &pin_violation_count);
  void checkGate(dbNet* net, dbWireGraph::Node* gate,
                 vector<ARinfo> &CARtable,
                 vector<ARinfo> &VIA_CARtable,
                 bool report,
                 bool verbose,
                 // Return values.
                 bool &violation,
                 std::unordered_set<dbWireGraph::Node*> &violated_gates);
  bool checkViolation(PARinfo &par_info, dbTechLayer* layer);
  bool antennaRatioDiffDependent(dbTechLayer* layer);

  void findWireRootIterms(dbWireGraph::Node* node,
                          int wire_level,
                          vector<dbITerm*>& gates);
  double diffArea(dbMTerm* mterm);
  double gateArea(dbMTerm* mterm);

  vector<std::pair<double, vector<dbITerm*>>> parMaxWireLength(
      dbNet* net,
      int layer);
  vector<std::pair<double, vector<dbITerm*>>> getViolatedWireLength(
      dbNet* net,
      int routing_level);

  odb::dbDatabase* db_;
  odb::dbBlock* block_;
  int dbu_per_micron_;
  grt::GlobalRouter* global_router_;
  utl::Logger* logger_;
  std::map<odb::dbTechLayer*, AntennaModel> layer_info_;
  int net_violation_count_;

  static constexpr int max_diode_count_per_gate = 10;

  // A map indexed by: pair(net, routing_level)
  // It stores the maximum length allowed to be held by that wire in the given layer without violating the antenna rules as
  // well as the current wire length.
  std::map<NetLayerPair, std::pair<double, double>> allowed_wire_length;

};

}  // namespace ant
