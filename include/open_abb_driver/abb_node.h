#include <errno.h>
#include <fcntl.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h> 

#include "open_abb_driver/abb_comm.h"
#include "open_abb_driver/matvec/matVec.h"

//ROS specific
#include <ros/ros.h>

#include <open_abb_driver/robot_Ping.h>
#include <open_abb_driver/robot_SetCartesian.h>
#include <open_abb_driver/robot_GetCartesian.h>
#include <open_abb_driver/robot_SetWorkObject.h>
#include <open_abb_driver/robot_SetZone.h>
#include <open_abb_driver/robot_SetTool.h>
#include <open_abb_driver/robot_SetComm.h>
#include <open_abb_driver/robot_SetJoints.h>
#include <open_abb_driver/robot_GetJoints.h>
#include <open_abb_driver/robot_SetSpeed.h>
#include <open_abb_driver/robot_SetDIO.h>
#include <open_abb_driver/robot_SpecialCommand.h>
#include <open_abb_driver/robot_Stop.h>
#include <open_abb_driver/robot_SetTrackDist.h>
#include <open_abb_driver/robot_IsMoving.h>

//ROS specific, these are redundant with abb_node
//standard libary messages instead of custom messages
#include <tf/transform_broadcaster.h>
#include <sensor_msgs/JointState.h>
#include <geometry_msgs/WrenchStamped.h>
#include <geometry_msgs/PoseStamped.h>

//#define MAX_BUFFER 256
#define MAX_BUFFER 10000
#define ID_CODE_MAX 999

#define SERVER_BAD_MSG 0
#define SERVER_OK 1
#define SERVER_COLLISION 2

#define MAX_TRANS_STEP 2.0
#define MAX_ROT_STEP (0.5 * DEG2RAD)
#define MAX_J_STEP 0.5

#define NB_FREQ 200.0
#define STOP_CHECK_FREQ 25.0
#define DIST_CHECK_FREQ 100.0

#define SAFETY_FACTOR 0.90
#define MINIMUM_TRACK_DIST_TRANS 1.0 //mm
#define MAXIMUM_TRACK_DIST_TRANS 20.0 //mm
#define MINIMUM_TRACK_DIST_ORI 0.333  //deg
#define MAXIMUM_TRACK_DIST_ORI 6.66 //deg
#define INFINITY_TRACK_DIST_TRANS 1000.0 ///mm
#define INFINITY_TRACK_DIST_ORI 333.0 //deg

#define MINIMUM_NB_SPEED_TCP 1.0 //mm/s
#define MINIMUM_NB_SPEED_ORI 0.333 //deg/s


#define NUM_JOINTS 6
#define NUM_FORCES 6

#define BLOCKING 1
#define NON_BLOCKING 0

namespace open_abb_driver
{
	
typedef enum
{
  ZONE_FINE = 0,
  ZONE_1,
  ZONE_2,
  ZONE_3,
  ZONE_4,
  ZONE_5,
  NUM_ZONES
} ZONE_TYPE;

typedef struct
{
  double p_tcp; // TCP path zone (mm)
  double p_ori; // Zone size for orientation (mm)
  double ori;   // Tool orientation (degrees)
} zone_vals;

static const zone_vals zone_data[NUM_ZONES] = 
{
  // p_tcp (mm), p_ori (mm), ori (deg)
  {0.0,   0.0,  0.0},   // ZONE_FINE
  {0.3,   0.3,  0.03},  // ZONE_1
  {1.0,   1.0,  0.1},   // ZONE_2
  {5.0,   8.0,  0.8},   // ZONE_3
  {10.0,  15.0, 1.5},   // ZONE_4
  {20.0,  30.0, 3.0}    // ZONE_5
};

// Mutex used for threads
pthread_mutex_t nonBlockMutex;
pthread_mutex_t jointUpdateMutex;
pthread_mutex_t cartUpdateMutex;
pthread_mutex_t wobjUpdateMutex;
pthread_mutex_t forceUpdateMutex;
pthread_mutex_t sendRecvMutex;

class RobotController
{
 public:
  RobotController(ros::NodeHandle * n);
  virtual ~RobotController();

  // Initialize the robot
  bool init();

  // Service Callbacks
  bool robot_Ping(
      open_abb_driver::robot_Ping::Request& req, 
      open_abb_driver::robot_Ping::Response& res);
  bool robot_SetCartesian(
      open_abb_driver::robot_SetCartesian::Request& req, 
      open_abb_driver::robot_SetCartesian::Response& res);
  bool robot_GetCartesian(
      open_abb_driver::robot_GetCartesian::Request& req, 
      open_abb_driver::robot_GetCartesian::Response& res);
  bool robot_SetJoints(
      open_abb_driver::robot_SetJoints::Request& req, 
      open_abb_driver::robot_SetJoints::Response& res);
  bool robot_GetJoints(
      open_abb_driver::robot_GetJoints::Request& req, 
      open_abb_driver::robot_GetJoints::Response& res);
  bool robot_Stop(
      open_abb_driver::robot_Stop::Request& req, 
      open_abb_driver::robot_Stop::Response& res);
  bool robot_SetTool(
      open_abb_driver::robot_SetTool::Request& req, 
      open_abb_driver::robot_SetTool::Response& res);
  bool robot_SetWorkObject(
      open_abb_driver::robot_SetWorkObject::Request& req, 
      open_abb_driver::robot_SetWorkObject::Response& res);
  bool robot_SetComm(
      open_abb_driver::robot_SetComm::Request& req, 
      open_abb_driver::robot_SetComm::Response& res);
  bool robot_SpecialCommand(
      open_abb_driver::robot_SpecialCommand::Request& req,
      open_abb_driver::robot_SpecialCommand::Response& res);
  bool robot_SetDIO(
      open_abb_driver::robot_SetDIO::Request& req, 
      open_abb_driver::robot_SetDIO::Response& res);
  bool robot_SetSpeed(
      open_abb_driver::robot_SetSpeed::Request& req, 
      open_abb_driver::robot_SetSpeed::Response& res);
  bool robot_SetZone(
      open_abb_driver::robot_SetZone::Request& req, 
      open_abb_driver::robot_SetZone::Response& res);
  bool robot_SetTrackDist(
      open_abb_driver::robot_SetTrackDist::Request& req, 
      open_abb_driver::robot_SetTrackDist::Response& res);
  bool robot_IsMoving(
      open_abb_driver::robot_IsMoving::Request& req, 
      open_abb_driver::robot_IsMoving::Response& res);


  // Advertise Services and Topics
  void advertiseServices();
  void advertiseTopics();

  // Call back function for the logging which will be called by a timer event
  void logCallback(const ros::TimerEvent&);
  
  // Public access to the ROS node
  ros::NodeHandle *node;

  // Non-Blocking move variables
  bool non_blocking;  // Whether we are in non-blocking mode
  bool do_nb_move;    // Whether we are currently moving in non-blocking mode
  bool targetChanged; // Whether a new target was specified
  bool stopRequest;   // Set to true when we are trying to stop the robot
  bool stopConfirm;   // Set to true when the thread is sure it's stopped
  bool cart_move;     // True if we're doing a cartesian move, false if joint

  // Variables dealing with changing non-blocking speed and step sizes
  bool changing_nb_speed; // Overrides setSpeed safety
  double curCartStep;     // Largest cartesian stepsize during non-blocking
  double curOrientStep;   // Largest orientation step size during non-blocking
  double curJointStep;    // Largest joint step size during non-blocking
  double curDist[3];      // Max allowable tracking error (pos, ang, joint)

  // Most recent goal position, and the final target position
  matvec::Vec curGoalP;
  matvec::Quaternion curGoalQ;
  matvec::Vec curTargP;
  matvec::Quaternion curTargQ;
  double curGoalJ[NUM_JOINTS];
  double curTargJ[NUM_JOINTS];

  // Move commands are public so that the non-blocking thread can use it
  bool setCartesian(double x, double y, double z, 
		    double q0, double qx, double qy, double qz);
  bool setJoints(double position[]);

  // Functions that compute our distance from the current position to the goal
  double posDistFromGoal();
  double orientDistFromGoal();
  double jointDistFromGoal();

 private:
  // Socket Variables
  bool motionConnected;
  bool loggerConnected;
  int robotMotionSocket;
  int robotLoggerSocket;

  // Connect to servers on the robot
  bool connectMotionServer(const char* ip, int port);
  bool connectLoggerServer(const char* ip, int port);
  
  // Sets up the default robot configuration
  bool defaultRobotConfiguration();

  //handles to ROS stuff
  tf::TransformBroadcaster handle_tf;
  //Duplicates with standard messages
  ros::Publisher handle_JointsLog;
  ros::Publisher handle_ForceLog;
  ros::Publisher handle_CartesianLog;

  ros::ServiceServer handle_Ping;
  ros::ServiceServer handle_SetCartesian;
  ros::ServiceServer handle_GetCartesian;
  ros::ServiceServer handle_SetJoints;
  ros::ServiceServer handle_GetJoints;
  ros::ServiceServer handle_Stop;
  ros::ServiceServer handle_SetTool;
  ros::ServiceServer handle_SetWorkObject;
  ros::ServiceServer handle_SetSpeed;
  ros::ServiceServer handle_SetZone;
  ros::ServiceServer handle_SetTrackDist;
  ros::ServiceServer handle_SpecialCommand;
  ros::ServiceServer handle_SetComm;
  ros::ServiceServer handle_SetDIO;
  ros::ServiceServer handle_IsMoving;
 
  // Helper function for communicating with robot server
  bool sendAndReceive(char *message, int messageLength, 
      char*reply, int idCode=-1);

  // Internal functions that communicate with the robot
  bool ping();
  bool getCartesian(double &x, double &y, double &z, 
      double &q0, double &qx, double &qy, double &qz);
  bool getJoints(double &j1, double &j2, double &j3,
      double &j4, double &j5, double &j6);
  bool setTool(double x, double y, double z, 
    double q0, double qx, double qy, double qz);
  bool setWorkObject(double x, double y, double z, 
    double q0, double qx, double qy, double qz);
  bool specialCommand(int command, double param1=0, double param2=0, double param3=0, double param4=0, double param5=0);
  bool setDIO(int dio_num, int dio_state);
  bool setSpeed(double tcp, double ori);
  bool setZone(int z);
  bool stop_nb();

  // Check if robot is currently moving or not
  bool is_moving();

  // Functions to handle setting up non-blocking step sizes
  bool setTrackDist(double pos_dist, double ang_dist);
  bool setNonBlockSpeed(double tcp, double ori);

  // Robot State
  double curSpd[2];
  int curZone;
  matvec::Vec curToolP;
  matvec::Quaternion curToolQ;
  matvec::Vec curWorkP;
  matvec::Quaternion curWorkQ;
  tf::Transform curWobjTransform;


  // Robot Position and Force Information
  matvec::Vec curP;
  matvec::Quaternion curQ;
  double curJ[NUM_JOINTS];
  double curForce[NUM_FORCES];
  
  double normalizeQuaternion( double& q0, double& qx, double& qy, double& qz );
};

}