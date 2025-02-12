#include <iostream>
#include <rw/rw.hpp>
#include <rw/trajectory/LinearInterpolator.hpp>
#include <rw/invkin/ClosedFormIKSolverUR.hpp>
#include <rwlibs/pathplanners/rrt/RRTPlanner.hpp>
#include <rwlibs/pathplanners/rrt/RRTQToQPlanner.hpp>
#include <rwlibs/pathplanners/prm/PRMPlanner.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>
#include <rwlibs/pathoptimization/pathlength/PathLengthOptimizer.hpp>
#include <vector>
#include <rw/pathplanning/QConstraint.hpp>
#include <rw/kinematics/Kinematics.hpp>

#include <rw/pathplanning/PlannerConstraint.hpp>

#include <rw/math.hpp>
#include <string>
#include <stdexcept>

using namespace std;
using namespace rw::invkin;
using namespace rw::common;
using namespace rw::math;
using namespace rw::kinematics;
using namespace rw::loaders;
using namespace rw::models;
using namespace rw::pathplanning;
using namespace rw::proximity;
using namespace rw::trajectory;
using namespace rwlibs::pathplanners;
using namespace rwlibs::proximitystrategies;

#define MAXTIME 30
#define DEBUG 0
#define OUT 2
#define ONLYRRT 0

#define TableXSurface -0.7 //1.5  // REAL 40.25  //sqrt(20²+8²+34²)
#define TableYSurface 0.1 //1.5  // REAL 40.25  //sqrt(20²+8²+34²)

#define Extend 0.05


/**
 * CollisionChecker Function
 **/
bool checkCollisions(Device::Ptr device, const State &state, const CollisionDetector &detector, const Q &q) {
    State testState;
    CollisionDetector::QueryResult data;
    bool colFrom;

    testState = state;
    device->setQ(q,testState);
    colFrom = detector.inCollision(testState,&data);
    if (colFrom) {
        cerr << "Configuration in collision: " << q << endl;
        cerr << "Colliding frames: " << endl;
        FramePairSet fps = data.collidingFrames;
        for (FramePairSet::iterator it = fps.begin(); it != fps.end(); it++) {
            cerr << (*it).first->getName() << " " << (*it).second->getName() << endl;
        }
        return false;
    }
    return true;
}

/**
 * getPoint: Get 3D point given robot, frame configuration and Q state
 **/
Transform3D<> getPoint(WorkCell::Ptr wc,Device::Ptr device, const State &state, const Q &q) {
    State testState;
    testState = state;
    device->setQ(q, testState);
    return Kinematics::worldTframe(wc->findFrame("PG70"), testState);
}


/**
 * Distance:
 **/
double distance(Transform3D<> pT1, Transform3D<> pT2) {

    Vector3D<> p1 = pT1.P();
    Vector3D<> p2 = pT2.P();
    Vector3D<> d = Vector3D<>(dot(p2.x(),p2)-dot(p1.x(),p1),dot(p2.y(),p2)-dot(p1.y(),p1),dot(p2.z(),p2)-dot(p1.z(),p1));
    return d.norm2();
}

double getTdistance(rw::trajectory::QPath path,WorkCell::Ptr wc,Device::Ptr device, const State &state) {

    double total = 0;
    for (QPath::iterator it = path.begin(); it < (path.end()-1); it++) {
        Q first = *(it);
        Q second =*(it+1) ;

        total += distance(getPoint(wc,device, state, first) , getPoint(wc,device, state, second) );

    }
    return total;
}


/**
 * CheckBorder: Check if given a Q value of the robot the x,y,z point is inside of the Static Zone
 **/
bool checkBorder(WorkCell::Ptr wc,Device::Ptr device, const State &state, const Q &q) {
    // Alternatively check .x() .y() .z()
    Transform3D<> point3D = getPoint(wc,device, state, q);
    Vector3D<> d = point3D.P();
   // cout << dot(d,d.y()) << endl;
    if (dot(d,d.y()) > TableYSurface)
        return false;
//    if (dot(d,d.x()) <= TableXSurface)
    return true;
}

/**
 * Inverse Kinematics
 **/
Q inverseKinematics(const Device::Ptr device, const SerialDevice::Ptr sdevice, const State &state, Transform3D<double> TD){

    rw::invkin::ClosedFormIKSolverUR::Ptr urIK;
    urIK = new rw::invkin::ClosedFormIKSolverUR(sdevice, state);
    vector<Q> solutions = urIK->solve(TD, state);
    if (solutions.size() == 0) {
        cout << solutions.size() << endl;
        std::cout << "No solution found!"<< endl << endl << endl << endl << endl << endl << endl;
    }
    else
        if (DEBUG ==1)
            cout << "SOLUTION RETURNED!!!!!!!" << std::endl << std::endl << std::endl << std::endl << std::endl << std::endl << std::endl;
        return solutions[1];
}


rw::trajectory::QPath linearInterpolatedPath(const rw::math::Q& start, const rw::math::Q& end,
                                               const double total_duration = 10.0, const double duration_step = 1.0)
  {
    rw::trajectory::QLinearInterpolator interpolator(start, end, total_duration);
    rw::trajectory::QPath path;
    path.push_back(start);
    for (double t = duration_step; t <= (total_duration - duration_step); t += duration_step)
    {
      path.push_back(interpolator.x(t));
    }
    path.push_back(end);

    return path;
  }



/**
   * PRM Planner for Static Zone, the roadmap is calculated outside of the planner
*/
QPath prmPath(PRMPlanner* prm, WorkCell::Ptr wc,Transform3D<> start, Transform3D<> end,Device::Ptr device,SerialDevice::Ptr sdevice,State state,  QMetric::Ptr metric){

    QPath path;
    // Constraints
    CollisionDetector detector(wc, ProximityStrategyFactory::makeDefaultCollisionStrategy());

    // Calculate Inverse
    Q from = inverseKinematics(device, sdevice, state, start);
    Q to = inverseKinematics(device, sdevice, state, end);
    // First Checking
    if (!checkCollisions(device, state, detector, from))
        return 0;
    if (!checkCollisions(device, state, detector, to))
        return 0;

    /*** Evaluation and Path calculation ***/
    if (DEBUG ==1)
        cout << "Calculate PRM path from " << from << " to " << to << endl;

    //Call planner
    prm->query(from,to,path,MAXTIME);

    return path;
  }
/**
   * PRM Planner for Static Zone, the roadmap is calculated outside of the planner
*/
QPath prmPath(WorkCell::Ptr wc,Transform3D<> start, Transform3D<> end,Device::Ptr device,SerialDevice::Ptr sdevice,State state,  QMetric::Ptr metric){

    // Constraints
    QPath path;
    CollisionDetector detector(wc, ProximityStrategyFactory::makeDefaultCollisionStrategy());
    PRMPlanner *prm = new PRMPlanner(device.get(), state, &detector, Extend); //input device as (rw::models::Device*)
    prm->setCollisionCheckingStrategy(PRMPlanner::LAZY);
    prm->setNeighSearchStrategy(PRMPlanner::BRUTE_FORCE);
    prm->setShortestPathSearchStrategy(PRMPlanner::A_STAR);
    prm->buildRoadmap(5000);

    // Calculate Inverse
    Q from = inverseKinematics(device, sdevice, state, start);
    Q to = inverseKinematics(device, sdevice, state, end);
    // First Checking
    if (!checkCollisions(device, state, detector, from))
        return 0;
    if (!checkCollisions(device, state, detector, to))
        return 0;

    /*** Evaluation and Path calculation ***/
    if (DEBUG ==1)
        cout << "Calculate PRM path from " << from << " to " << to << endl;

    //Call planner
    prm->query(from,to,path,MAXTIME);

    return path;
  }


/***
 * RRT Planner for Dynamic Zone
 **/
QPath rrtPath(WorkCell::Ptr wc,Transform3D<> start,Transform3D<>  end,Device::Ptr device,SerialDevice::Ptr sdevice,State state,  QMetric::Ptr metric){

  QPath path;
  // Constraints
  CollisionDetector detector(wc, ProximityStrategyFactory::makeDefaultCollisionStrategy());
  PlannerConstraint constraint = PlannerConstraint::make(&detector,device,state);
  QSampler::Ptr sampler = QSampler::makeConstrained(QSampler::makeUniform(device),constraint.getQConstraintPtr());
  QToQPlanner::Ptr rrt = RRTPlanner::makeQToQPlanner(constraint, sampler, metric, Extend, RRTPlanner::RRTConnect);

  // Calculate Inverse
  Q from = inverseKinematics(device, sdevice, state, start);
  Q to = inverseKinematics(device, sdevice, state, end);
  // First Checking
  if (!checkCollisions(device, state, detector, from))
      return 0;
  if (!checkCollisions(device, state, detector, to))
      return 0;

  /*** Evaluation and Path calculation ***/
  if (DEBUG ==1)
    cout << "Calculate  RRT path from " << from << " to " << to << endl;

  rrt->query(from,to,path,MAXTIME); //run planner

  return path;
}

/***
 * RRT Planner for Dynamic Zone
 **/
QPath rrtPath(WorkCell::Ptr wc,Q from,Transform3D<>  end,Device::Ptr device,SerialDevice::Ptr sdevice,State state,  QMetric::Ptr metric){

  QPath path;
  // Constraints
  CollisionDetector detector(wc, ProximityStrategyFactory::makeDefaultCollisionStrategy());
  PlannerConstraint constraint = PlannerConstraint::make(&detector,device,state);
  QSampler::Ptr sampler = QSampler::makeConstrained(QSampler::makeUniform(device),constraint.getQConstraintPtr());
  QToQPlanner::Ptr rrt = RRTPlanner::makeQToQPlanner(constraint, sampler, metric, Extend, RRTPlanner::RRTConnect);

  // Calculate Inverse
  //Q from = inverseKinematics(device, sdevice, state, start);
  Q to = inverseKinematics(device, sdevice, state, end);
  // First Checking
  if (!checkCollisions(device, state, detector, from))
      return 0;
  if (!checkCollisions(device, state, detector, to))
      return 0;

  /*** Evaluation and Path calculation ***/
  if (DEBUG ==1)
    cout << "Calculate  RRT path from " << from << " to " << to << endl;

  rrt->query(from,to,path,MAXTIME); //run planner

  return path;
}



QPath pathPlanner(PRMPlanner* prm, WorkCell::Ptr wc,Transform3D<> FROM, Transform3D<> TO,Device::Ptr device,SerialDevice::Ptr sdevice,State state,  QMetric::Ptr metric){

    int i = 0;
    Q lastCorrect;
    QPath pathPRM, pathRRT,path;
    if (ONLYRRT == 1){
        pathRRT = rrtPath(wc,FROM,TO,device,sdevice,state,metric);
        path = pathRRT;
    }
    else {
        pathPRM = prmPath(prm,wc,FROM,TO,device,sdevice,state, metric);
        /*** Output Path ***/
        for (QPath::iterator it = pathPRM.begin(); it < pathPRM.end(); it++) {
            //cout << *it << endl;
            // Check if the points of the path are outside of the Static Zone
            if (checkBorder(wc,device, state, *it) == 0){
                if (it == pathPRM.begin())
                    lastCorrect = *(pathPRM.begin());
                else
                    lastCorrect = *(--it);
                // Get the point of the new start of the dynamic zone
               //cout << "BARK " << i << " times"<<endl;
                //cout << lastCorrect<<endl;
                //Transform3D<> newStart = getPoint(wc,device, state, lastCorrect);
                pathRRT = rrtPath(wc,lastCorrect,TO,device,sdevice,state,metric);
                // Merge the paths
                path = QPath(pathPRM.begin(),pathPRM.begin()+i);
                copy(pathRRT.begin(), pathRRT.end(), std::inserter(path, path.end()));
                break;
            }
            i++;
        }
    }
  return path;
}


int main() {

    /*** Load Cell and device ***/
    const string wcFile = "/home/ali/WorkSpace/Rovi/WorkCell_scenes/WorkStation_Medium/WC1_Scene.wc.xml";
    const string deviceName = "UR1";
    int i = 0;

    WorkCell::Ptr wc = WorkCellLoader::Factory::load(wcFile);
    State state = wc->getDefaultState();
    Device::Ptr device = wc->findDevice(deviceName);
    SerialDevice::Ptr sdevice = wc->findDevice<SerialDevice>(deviceName);
    cout << "Trying to use workcell " << wcFile << " and device " << deviceName << endl;
    if (device == NULL) {
        cerr << "Device: " << deviceName << " not found!" << endl;
        return 0;
    }
    if (sdevice == NULL) {      // Some methods require serial devices
        cerr << "SerialDevice: " << deviceName << " not found!" << endl;
        return 0;
    }
    //------------------------------------------------------------------------------------------
//        //Points for Easy WorkCell

//      /** Points of our Planning **/
//      Vector3D<> sp1(-0.18, -0.06, 0.82); RPY<> sr1(-1.3, -1.5, 1.5);
//      Vector3D<> ep1(0.0, -0.82, 0.28); RPY<> er1(-1.4, 0.0, -3.0);
//      // Transformation Matrix
//      Transform3D<> F1(sp1, sr1.toRotation3D()); Transform3D<> T1(ep1, er1.toRotation3D());

//      Vector3D<> sp2(-0.18, -0.06, 0.82); RPY<> sr2(-1.3, -1.5, 1.5);
//      Vector3D<> ep2(0.18, -0.82, 0.28); RPY<> er2(-1.4, 0.0, -3.0);
//      // Transformation Matrix
//      Transform3D<> F2(sp2, sr2.toRotation3D()); Transform3D<> T2(ep2, er2.toRotation3D());

//      Vector3D<> sp3(-0.18, -0.06, 0.82); RPY<> sr3(-1.3, -1.5, 1.5);
//      Vector3D<> ep3(0.18, -0.82, 0.28); RPY<> er3(-1.4, 0.0, -3.0);
//      // Transformation Matrix
//      Transform3D<> F3(sp3, sr3.toRotation3D()); Transform3D<> T3(ep3, er3.toRotation3D());

//      Vector3D<> sp4(-0.18, -0.06, 0.82); RPY<> sr4(-1.3, -1.5, 1.5);
//      Vector3D<> ep4(0.049, -0.92, 0.178); RPY<> er4(-1.3, -0.3, -3.1);
//      // Transformation Matrix
//      Transform3D<> F4(sp4, sr4.toRotation3D()); Transform3D<> T4(ep4, er4.toRotation3D());

//      Vector3D<> sp5(-0.18, -0.06, 0.82); RPY<> sr5(-1.3, -1.5, 1.5);
//      Vector3D<> ep5(0.30, -0.80, 0.28); RPY<> er5(-0.9, 0.0, 2.5);
//      // Transformation Matrix
//      Transform3D<> F5(sp5, sr5.toRotation3D()); Transform3D<> T5(ep5, er5.toRotation3D());

        //--------------------------------------------------------------------------------------------

        //------------------------------------------------------------------------------------------

      //------------------------------------------------------------------------------------------

        //Points for Medium WorkCell
        /** Points of our Planning **/
        //Vector3D<> sp1(0.47, -0.65, 0.28); RPY<> sr1(-0.7, 0.1, 2.8);
        Vector3D<> sp1(0.35, -0.65, 0.28); RPY<> sr1(-0.7, 0.1, 2.8);
        Vector3D<> ep1(-0.38, -0.69, 0.28); RPY<> er1(-0.5, 1.3, 1.2);
        // Transformation Matrix
        Transform3D<> F1(sp1, sr1.toRotation3D()); Transform3D<> T1(ep1, er1.toRotation3D());

        Vector3D<> sp2(0.35, -0.65, 0.28); RPY<> sr2(-0.7, 0.1, 2.8);
        Vector3D<> ep2(-0.3, -0.74, 0.28); RPY<> er2(-0.4, 1.3, 1.2);
        // Transformation Matrix
        Transform3D<> F2(sp2, sr2.toRotation3D()); Transform3D<> T2(ep2, er2.toRotation3D());

        //Vector3D<> sp3(0.35, -0.75, 0.28); RPY<> sr3(-0.7, 0.1, 2.8);
        Vector3D<> sp3(0.35, -0.65, 0.28); RPY<> sr3(-0.7, 0.1, 2.8);
        Vector3D<> ep3(-0.45, -0.61, 0.34); RPY<> er3(1.6, 0.4, 3.0);
        // Transformation Matrix
        Transform3D<> F3(sp3, sr3.toRotation3D()); Transform3D<> T3(ep3, er3.toRotation3D());

        //Vector3D<> sp4(0.35, -0.63, 0.28); RPY<> sr4(-0.7, 0.1, 2.8);
        Vector3D<> sp4(0.35, -0.65, 0.28); RPY<> sr4(-0.7, 0.1, 2.8);
        //Vector3D<> ep4(-0.47, -0.66, 0.47); RPY<> er4(-2.3, -1.4, -1.8);
        Vector3D<> ep4(-0.51, -0.45, 0.29); RPY<> er4(0.7, 0.5, 2.7);
        // Transformation Matrix
        Transform3D<> F4(sp4, sr4.toRotation3D()); Transform3D<> T4(ep4, er4.toRotation3D());

//        Vector3D<> sp5(0.42, -0.74, 0.28); RPY<> sr5(-0.6, 0.1, 2.8);
        Vector3D<> sp5(0.35, -0.65, 0.28); RPY<> sr5(-0.7, 0.1, 2.8);
        Vector3D<> ep5(-0.36, -0.61, 0.18); RPY<> er5(1.0, 0.4, 2.7);
        // Transformation Matrix
        Transform3D<> F5(sp5, sr5.toRotation3D()); Transform3D<> T5(ep5, er5.toRotation3D());

        //--------------------------------------------------------------------------------------------

        Transform3D<> Sp[] = {F1,F2,F3,F4,F5};
        Transform3D<> Ep[] = {T1,T2,T3,T4,T5};


    /***  Get Path  ***/
    // Constraints
    Timer local,total;
    QMetric::Ptr metric = MetricFactory::makeEuclidean<Q>();
    CollisionDetector detector(wc, ProximityStrategyFactory::makeDefaultCollisionStrategy());
    PlannerConstraint constraint = PlannerConstraint::make(&detector,device,state);
    PRMPlanner* prm ;
    // Build the road map just once
    if (ONLYRRT == 0) {
        prm = new PRMPlanner(device.get(), state, &detector, Extend); //input device as (rw::models::Device*)
        prm->setCollisionCheckingStrategy(PRMPlanner::LAZY);
        prm->setNeighSearchStrategy(PRMPlanner::BRUTE_FORCE);
        prm->setShortestPathSearchStrategy(PRMPlanner::A_STAR);
        prm->buildRoadmap(10000);
    }
    /*** Change the start/end values and create different queries ***/
   // int k = 0;
   // for (k=0;k<10;k++){
   //     i = 0;
    int k = 0;
    for (k=0;k<1;k++){
        i = 0;
        while (i < 5){
           // cout <<Sp[i] << "   "<< Ep[i] << endl;
            local.resetAndResume();
            // Point Selection //
            Transform3D<> FROM = Sp[i];
            Transform3D<> TO = Ep[i];


            // Call to Planer
            QPath path = pathPlanner(prm, wc,FROM,TO,device,sdevice,state, metric);

            /** Path Optimization **/
            rwlibs::pathoptimization::PathLengthOptimizer PathLengthOptimizer(constraint, metric); // Optimize Pathpranning
            QPath pathOpti= PathLengthOptimizer.pathPruning(path); // Optimize Path using PathPruning method
            //QPath pathOpti= PathLengthOptimizer.shortCut(path); // Optimize Path using shortcut method

            local.pause();

            /*** Output Path ***/
            if (OUT == 1){
                cout << "Number of Nodes " << path.size() << " found in " << local.getTime() << " seconds." << endl;
                cout << "Length PATH NO OPT "<<getTdistance(path,wc,device,state)<< endl;

                cout << "Number of Nodes Optimized " << pathOpti.size() << " found "<< endl;
                cout << "Length PATH OPTI "<<getTdistance(pathOpti,wc,device,state)<< endl;
                cout << " Total time: " << local.getTime() << " seconds." << endl << endl;
            }
                else if (OUT == 2){
                    if (local.getTime() >= MAXTIME) {
                        cout << "Notice: max time of " << MAXTIME << " seconds reached." << endl;
                    }
                    // Original Path
                    cout << "Original Path" << endl;
                    for (QPath::iterator it = path.begin(); it < path.end(); it++) {
                        cout << *it << endl;
                    }

                    cout << "......................................" << endl<< endl;

                    // Optimized Path
                    cout << "Optimized Path" << endl;
                    for (QPath::iterator it = pathOpti.begin(); it < pathOpti.end(); it++) {
                        cout << *it << endl;
                    }
                    cout << endl;
                    cout << endl;
            }
            else {
               cout << local.getTime() << ";" << getTdistance(pathOpti,wc,device,state) << ";" << pathOpti.size() << endl;
            }
            i++;
        }
    }
    local.pause();
    cout << " Total time: " << total.getTime() << " seconds." << endl;
    return 0;

}
