// 
//     MINOTAUR -- It's only 1/2 bull
// 
//     (C)opyright 2008 - 2021 The MINOTAUR Team.
//
/**
 * \file BranchAndBound.cpp
 * \brief Define BranchAndBound class for a generic branch-and-bound method.
 * \author Ashutosh Mahajan, Argonne National Laboratory
 */

#include <cmath>
#include <iomanip>

#include "MinotaurConfig.h"
#include "BranchAndBound.h"


//#define DEBUG 1
//#define SPEW 1

using namespace Minotaur;

const std::string BranchAndBound::me_ = "BranchAndBound: ";

BranchAndBound::BranchAndBound()
  : env_(0),
    nodePrcssr_(),
    nodeRlxr_(0),
    options_(0),
    problem_(0),
    solPool_(0), 
    stats_(0),
    status_(NotStarted),
    timer_(0),
    tm_(0)
{
}


BranchAndBound::BranchAndBound(EnvPtr env, ProblemPtr p) 
  : env_(env),
    nodePrcssr_(0),
    nodeRlxr_(0),
    problem_(p),
    solPool_(0),
    stats_(0),
    status_(NotStarted)
{
  timer_ = env->getNewTimer();
  tm_ = (TreeManagerPtr) new TreeManager(env);
  options_ = (BabOptionsPtr) new BabOptions(env);
  logger_ = env->getLogger();
}


BranchAndBound::~BranchAndBound()
{
  problem_ = 0;
  env_ = 0;
  nodeRlxr_ = 0;
  nodePrcssr_ = 0;
  if (options_) {
    delete options_;
  }
  if (solPool_) {
    delete solPool_;
  }
  if (stats_) {
    delete stats_;
  }
  if (timer_) {
    delete timer_;
  }
  if (tm_) {
    delete tm_;
  }
  for (HeurVector::iterator it=preHeurs_.begin(); it!=preHeurs_.end(); ++it) {
    delete *it;
  }
  preHeurs_.clear();
}


void BranchAndBound::addPreRootHeur(HeurPtr h)
{
  preHeurs_.push_back(h);
}


double BranchAndBound::getPerGap() 
{ 
  return tm_->getPerGap(); 
}


double BranchAndBound::getLb() 
{
  return tm_->getLb();
}


NodeProcessorPtr BranchAndBound::getNodeProcessor()
{
  return nodePrcssr_;
}


NodeRelaxerPtr BranchAndBound::getNodeRelaxer()
{
  return nodeRlxr_;
}


SolutionPtr BranchAndBound::getSolution()
{
  return solPool_->getBestSolution();
}


//SolutionPtr BranchAndBound::getRootSolution()
//{
// return solPool_->getRootSolution();
//}


SolveStatus BranchAndBound::getStatus()
{
  return status_;
}


TreeManagerPtr BranchAndBound::getTreeManager() 
{
  return tm_;
}


double BranchAndBound::getUb() 
{
  return tm_->getUb();
}


UInt BranchAndBound::numProcNodes()
{
  return stats_->nodesProc;
}


NodePtr BranchAndBound::processRoot_(bool *should_prune, bool *should_dive)
{
  NodePtr current_node = (NodePtr) new Node ();
  NodePtr new_node = NodePtr(); // NULL
  RelaxationPtr rel;
  bool prune = *should_prune;
  Branches branches = 0;
  WarmStartPtr ws;
#if SPEW
  logger_->msgStream(LogDebug) << me_ << "creating root node" << 
    std::endl;
#endif
  tm_->insertRoot(current_node);

  if (options_->createRoot == true) {
    rel = nodeRlxr_->createRootRelaxation(current_node, prune);
    rel->setProblem(problem_);
  } else {
    rel = nodeRlxr_->getRelaxation();
  }

  if (!prune) {
  // solve the root node only if the initial root relaxation is not pruned
  // because of the unboundedness or infeasibility
#if SPEW
  logger_->msgStream(LogDebug) << me_ << "processing root node" << 
    std::endl;
#endif
  
    nodePrcssr_->processRootNode(current_node, rel, solPool_);
    ++stats_->nodesProc;
    if (nodePrcssr_->foundNewSolution()) {
      tm_->setUb(solPool_->getBestSolutionValue());
    }
    
    prune = shouldPrune_(current_node);
  }
  if (prune) {
    nodeRlxr_->reset(current_node, false);
    tm_->pruneNode(current_node);
    tm_->removeActiveNode(current_node);
  } else {
#if SPEW
    logger_->msgStream(LogDebug1) << me_ << "branching in root" << 
      std::endl;
#endif
    // branch.
    branches = nodePrcssr_->getBranches();
    ws = nodePrcssr_->getWarmStart();
    tm_->removeActiveNode(current_node);
    *should_dive = tm_->shouldDive();
    new_node = tm_->branch(branches, current_node, ws);
    assert((*should_dive && new_node) || (!(*should_dive) && !new_node));
    if (!(*should_dive)) {
      nodeRlxr_->reset(current_node, false);
      new_node = tm_->getCandidate();
      assert(new_node);
    }
  }
  current_node = new_node;

  tm_->updateLb();

  showStatus_(*should_dive);
  *should_prune = prune;
  return current_node;
}


void BranchAndBound::setLogLevel(LogLevel level) 
{
  logger_->setMaxLevel(level);
}


void BranchAndBound::setNodeProcessor(NodeProcessorPtr p)
{
  nodePrcssr_ = p;
}


void BranchAndBound::setNodeRelaxer(NodeRelaxerPtr nr)
{
  nodeRlxr_ = nr;
}


void BranchAndBound::shouldCreateRoot(bool b)
{
  options_->createRoot = b;
}


bool BranchAndBound::shouldPrune_(NodePtr node)
{
  bool should_prune = false;
  switch (node->getStatus()) {
   case (NodeOptimal):
     should_prune = true;
     break;
   case (NodeHitUb):
     should_prune = true;
     break;
   case (NodeInfeasible):
     should_prune = true;
     break;
   case (NodeDominated):
     should_prune = true;
     break;
   case (NodeStopped):
     assert(!"implement me!");
     break;
   case (NodeContinue):
     should_prune = false;
     break;
   case (NodeNotProcessed):
     assert(!"node not processed, still being asked if it should be pruned");
     break;
  }
  return should_prune;
}


bool BranchAndBound::shouldStop_()
{
  bool stop_bnb = false;

  if (tm_->getPerGap() <= 0.0) {
    stop_bnb = true;
    status_ = SolvedOptimal;
  } else if ( tm_->getPerGap() <= options_->perGapLimit) {
    stop_bnb = true;
    status_ = SolvedGapLimit;
  } else if (timer_->query() > options_->timeLimit) {
    stop_bnb = true;
    status_ = TimeLimitReached;
  } else if (stats_->nodesProc >= options_->nodeLimit) {
    stop_bnb = true;
    status_ = IterationLimitReached;
  } else if (solPool_->getNumSolsFound()>=options_->solLimit) { 
    stop_bnb = true;
    status_ = SolLimitReached;
  }

  return stop_bnb;
}

void BranchAndBound::showStatus_(bool current_uncounted)
{
  static bool header = false;
  static bool firstRow = true; // Add a flag for the first row

  UInt off = 0;
  if (current_uncounted) {
    off = 1;
  }

  std::streamsize defaultPrecision = logger_->msgStream(LogError).precision(); // Store default precision

  if (!header) {
    logger_->msgStream(LogError) << " " << std::endl;	  
    logger_->msgStream(LogError) << "---------------------------------------------------------------------------------------------" << std::endl;
    logger_->msgStream(LogError) << std::setw(6) << "Cpu(s)"
      << std::setw(10) << "Wall(s)"
      << std::setw(16) << "LB"
      << std::setw(13) << "UB"
      << std::setw(12) << "Gap%"
      << std::setw(15) << "   Nodes-Proc"
      << std::setw(14) << "   Nodes-Rem"
      << std::setw(7) << "#Sol"
      << std::endl;
    logger_->msgStream(LogError) << "---------------------------------------------------------------------------------------------" << std::endl;
    header = true;
  }

  if (firstRow) {
    // Print the initial row with all values set to zero
    logger_->msgStream(LogError) << std::setw(5) << "0"
      << std::setw(10) << "0"
      << std::setw(17) << "-inf"
      << std::setw(13) << "inf"
      << std::setw(12) << "inf"
      << std::setw(15) << "0"
      << std::setw(14) << "0"
      << std::setw(7) << "0"
      << std::endl;
    firstRow = false;
  }

  if (timer_->query() - stats_->updateTime > options_->logInterval) {
    logger_->msgStream(LogError).precision(defaultPrecision); // Reset precision to default
    logger_->msgStream(LogError) << std::setw(5) << std::fixed << std::setprecision(0) << timer_->query()
      << std::setw(10) << std::fixed << std::setprecision(0) << timer_->wQuery() // Print wall time
      << std::setw(17) << std::setprecision(4) << std::scientific << tm_->updateLb()
      << std::setw(13) << std::setprecision(4) << std::scientific << tm_->getUb()
      << std::setw(12) << std::setprecision(2) << std::fixed << tm_->getPerGap()
      << std::setw(15) << tm_->getSize() - tm_->getActiveNodes() - off
      << std::setw(14) << tm_->getActiveNodes() + off
      << std::setw(7) << std::setprecision(5) << solPool_->getNumSols()
      << std::endl;
    stats_->updateTime = timer_->query();
  }
}

void BranchAndBound::solve()
{
  bool should_dive = false, dived_prev = false;
  bool should_prune = false;
  NodePtr current_node = NodePtr();
  NodePtr new_node = NodePtr();
  Branches branches = 0;
  WarmStartPtr ws;
  RelaxationPtr rel = RelaxationPtr();
  bool should_stop = false;

  // initialize timer
  timer_->start();
  logger_->msgStream(LogInfo) << me_ << "starting branch-and-bound"
    << std::endl;

  // get problem size and statistics to detect problem type.
  problem_->calculateSize();
#if SPEW
  problem_->writeSize(logger_->msgStream(LogExtraInfo));
#endif

  // initialize statistics
  if (stats_) {
    delete stats_;
  }
  stats_ = new BabStats();

  // initialize solution pool
  // TODO: use user options to set the pool size. For now it is 1.
  solPool_ = (SolutionPoolPtr) new SolutionPool(env_, problem_, 1);

  // call heuristics before the root, if needed 
  for (HeurVector::iterator it=preHeurs_.begin(); it!=preHeurs_.end(); ++it) {
    (*it)->solve(current_node, rel, solPool_);
  }
  tm_->setUb(solPool_->getBestSolutionValue());

  // do the root
  current_node = processRoot_(&should_prune, &dived_prev);

  // stop if done
  if (!current_node) {
    tm_->updateLb();
    if (tm_->getUb() <= -INFINITY) {
      status_ = SolvedUnbounded;
    } else  if (tm_->getUb() < INFINITY) {
      status_ = SolvedOptimal; 
    } else {
      status_ = SolvedInfeasible; 
    }
#if SPEW
    logger_->msgStream(LogDebug) << me_ << "stopping after root node "
      << std::endl;
#endif
    should_stop = true;
  } else if (shouldStop_()) {
    tm_->updateLb();
    should_stop = true;
  } else {
#if SPEW
    logger_->msgStream(LogDebug) << std::setprecision(8)
      << me_ << "lb = " << tm_->updateLb() << std::endl
      << me_ << "ub = " << tm_->getUb() << std::endl;
#endif
  }


  // solve root outside the loop. save the useful information.
  while(should_stop == false) {
#if SPEW
    logger_->msgStream(LogDebug1) << me_ << "processing node "
      << current_node->getId() << std::endl
      << me_ << "depth = " << current_node->getDepth() << std::endl
      << me_ << "did we dive = " << dived_prev << std::endl;
#endif

    should_dive = false;
    rel = nodeRlxr_->createNodeRelaxation(current_node, dived_prev, 
                                          should_prune);
    nodePrcssr_->process(current_node, rel, solPool_);

    ++stats_->nodesProc;
#if SPEW
    logger_->msgStream(LogDebug1) << me_ << "node lower bound = " << 
      current_node->getLb() << std::endl;
#endif

    if (nodePrcssr_->foundNewSolution()) {
      tm_->setUb(solPool_->getBestSolutionValue());
    }
    
    should_prune = shouldPrune_(current_node);
    if (should_prune) {
#if SPEW
      logger_->msgStream(LogDebug1) << me_ << "node pruned" << 
        std::endl;
#endif
      nodeRlxr_->reset(current_node, false);
      tm_->pruneNode(current_node);
      if (!dived_prev) {
        tm_->removeActiveNode(current_node);
      }
      new_node = tm_->getCandidate();
      dived_prev = false;
    } else {
#if SPEW
      logger_->msgStream(LogDebug1) << me_ << "branching" << 
        std::endl;
#endif
      branches = nodePrcssr_->getBranches();
   
      ws = nodePrcssr_->getWarmStart();
      if (!dived_prev) {
        tm_->removeActiveNode(current_node);
      }
      should_dive = tm_->shouldDive();
    
      new_node = tm_->branch(branches, current_node, ws);
      assert((should_dive && new_node) || (!should_dive && !new_node));
      if (should_dive) {
        dived_prev = true;
      } else {
        nodeRlxr_->reset(current_node, false);
        new_node = tm_->getCandidate(); // Can be NULL. The branches that were
                                        // created could have large lb and tm 
                                        // might have eliminated them.
        dived_prev = false;
      }
    }
    current_node = new_node;

    showStatus_(should_dive);

    // stop if done
    if (!current_node) {
      tm_->updateLb();
      if (tm_->getUb() <= -INFINITY) {
        status_ = SolvedUnbounded;
      } else if (tm_->getUb() < INFINITY) {
        status_ = SolvedOptimal; // TODO: get the right status
      } else {
        status_ = SolvedInfeasible; // TODO: get the right status
      }
#if SPEW
      logger_->msgStream(LogDebug) << me_ << "all nodes have "
        << "been processed" << std::endl;
#endif
      break;
    } else if (shouldStop_()) {
      tm_->updateLb();
      break;
    } else {
#if SPEW
      logger_->msgStream(LogDebug) << std::setprecision(8)
        << me_ << "lb = " << tm_->getLb() << std::endl 
        << me_ << "ub = " << tm_->getUb() << std::endl;
#endif
    }
  }
  logger_->msgStream(LogError) << " " << std::endl;
  logger_->msgStream(LogError) << "---------------------------------------------------------------------------------------------" << std::endl;
  logger_->msgStream(LogError) << " " << std::endl;

  logger_->msgStream(LogInfo) << me_ << "stopping branch-and-bound"
    << std::endl
    << me_ << "nodes processed = " << stats_->nodesProc << std::endl
    << me_ << "nodes created   = " << tm_->getSize() << std::endl;
  stats_->timeUsed = timer_->query();
  timer_->stop();
}


void BranchAndBound::writeStats(std::ostream &out)
{
  out << me_ << "time taken      = " << std::fixed << std::setprecision(2)
    << stats_->timeUsed << std::endl
    << me_ << "nodes processed = " << stats_->nodesProc << std::endl
    << me_ << "nodes created   = " << tm_->getSize() << std::endl;
  nodePrcssr_->writeStats(out);
  nodePrcssr_->getBrancher()->writeStats(out);
  for (HeurVector::iterator it=preHeurs_.begin(); it!=preHeurs_.end(); ++it) {
    (*it)->writeStats(out);
  }
  solPool_->writeStats(out);
}


double BranchAndBound::totalTime()
{
  return stats_->timeUsed;
}


// --------------------------------------------------------------------------
// --------------------------------------------------------------------------

BabStats::BabStats()
  :nodesProc(0),
   timeUsed(0),
   updateTime(0)
{
}


// --------------------------------------------------------------------------
// --------------------------------------------------------------------------
BabOptions::BabOptions()
  : createRoot(true),
    nodeLimit(0),
    perGapLimit(0.),
    solLimit(0),
    timeLimit(0.)
    
{
}


BabOptions::BabOptions(EnvPtr env)
{
  OptionDBPtr options = env->getOptions();

  logInterval = options->findDouble("bnb_log_interval")->getValue();
  nodeLimit   = options->findInt("bnb_node_limit")->getValue();
  perGapLimit = options->findDouble("obj_gap_percent")->getValue();
  solLimit    = options->findInt("bnb_sol_limit")->getValue();
  timeLimit   = options->findDouble("bnb_time_limit")->getValue();
  createRoot  = true;
}


// Local Variables: 
// mode: c++ 
// eval: (c-set-style "k&r") 
// eval: (c-set-offset 'innamespace 0) 
// eval: (setq c-basic-offset 2) 
// eval: (setq fill-column 78) 
// eval: (auto-fill-mode 1) 
// eval: (setq column-number-mode 1) 
// eval: (setq indent-tabs-mode nil) 
// End:
