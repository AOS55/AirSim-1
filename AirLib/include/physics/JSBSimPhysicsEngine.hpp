
#ifndef airsim_core_JSBSimPhysicsEngine_hpp
#define airsim_core_JSBSimPhysicsEngine_hpp

#include "PhysicsBody.hpp"
#include "common/Common.hpp"
#include "common/UpdatableObject.hpp"
#include "common/CommonStructs.hpp"
#include <JSBSim/FGFDMExec.h> // This is a absolute path should be relative, linker problems
// #include <JSBSim/initialization/FGInitialCondition.h> // This is a absolute path should be relative, linker problems
// #include <JSBSim/simgear/misc/sg_path.hxx> // This is a relative path should be relative, linker problems, shouldn't need this the header in FGFDMExec should invoke this for inheritance

/* #include "Airlib/deps/JSBSim/include/JSBSim/FGFDMExec.h"
#include "Airlib/deps/JSBSim/include/JSBSim/initialization/FGInitialCondition.h"
#include "Airlib/deps/JSBSim/include/JSBSim/simgear/misc/sg_path.hxx" */

#include "physics/Kinematics.hpp"
#include <math.h>


namespace msr { namespace airlib {

class JSBSimPhysicsEngine : public UpdatableObject {

public: //interface for virtual functions to be implemented by a derived class

	
	virtual void setCollisionInfo(const CollisionInfo& collision_info)
	{
		collision_info_ = collision_info;
	}

	CollisionResponse& getCollisionResponseInfo()
	{
		return collision_response_;
	}
	
public: //methods
	JSBSimPhysicsEngine()
	{
		initialize();
	}
	
	virtual void initialize()
	{
		jsbsim_aircraft = JSBSim::FGFDMExec(); // construct JSBSim FGFDMExec class
		loadJSBSimPaths(model_path_);
		const SGPath ic_file("/* Insert path to initial condition file */");
		JSBSim::FGInitialCondition* fgic = jsbsim_aircraft.GetIC();
		// fgic->Load(ic_file);  // had to include header file to include FGInitialCondition
		
		jsbsim_aircraft.Setdt(delta_t_);
		const bool success = jsbsim_aircraft.RunIC(); // causes sim time to reset to 0.0, returns true if successful
		if (!success)
		{
			std::cout << "JSBSim failed to initialize simulation conditions" << std::endl;
		}
	}

	virtual void update() override
	{
		UpdatableObject::update();
		jsbsim_aircraft.Run();
		updateKinematicState();
		updateControlState();
	}

	virtual void resetImplementation() override
	{
		const int no_output_reset_mode = 0;
		jsbsim_aircraft.ResetToInitialConditions(no_output_reset_mode); // multiple modes here not quite sure which is the best to set
	}

	void reportState(StateReporter& reporter) override
	{
		// call base implementation
		UpdatableObject::reportState(reporter);
	}

	// set the normalized control input, values are given between -1 and +1
	void setControlCommand(double aileron, double elevator, double throttle, double rudder)
	{
		setProperty("fcs/aileron-cmd-norm", aileron); // range -1 to +1
		setProperty("fcs/elevator-cmd-norm", elevator); // range -1 to +1
		setProperty("fcs/throttle-cmd-norm", throttle); // range 0 to +1 
		setProperty("fcs/rudder-cmd-norm", rudder); // range -1 ot +1 note x8 has no rudder
	}

	void setModelPath(std::string model_path)
	{
		model_path_ = model_path;
	}

	void setCollisionInfo(CollisionInfo& collision_info) const
	{
		collision_info = collision_info_;
	}

	Kinematics::State getKinematicState() const
	{
		return current_state_;
	}

	Pose& getPose()
	{
		return current_state_.pose;
	}

	vector<double>& getControlState()
	{
		return jsbsim_control_state_;
	}

	void setDeltaT(double delta_t)
	{
		delta_t_ = delta_t;
	}

	double getDeltaT() const
	{
		return delta_t_;
	}

	double getTime() const
	{
		return jsbsim_aircraft.GetSimTime();
	}
	
	JSBSim::FGFDMExec getJSBSim()
	{
		return jsbsim_aircraft;
	}

protected:

	void loadJSBSimPaths(std::string model_path)
	{
		SGPath aircraft_path("");
		SGPath engine_path("");
		SGPath system_path("");
		jsbsim_aircraft.LoadModel(aircraft_path, engine_path, system_path, model_path);
	}
	
	// get the the value of a property from JSBSim
	virtual double getProperty(const std::string& property_name)
	{
		const double property_value = jsbsim_aircraft.GetPropertyValue(property_name);
		return property_value;
	}

	// set a property value
	virtual void setProperty(const std::string& property_name, double property_value)
	{
		jsbsim_aircraft.SetPropertyValue(property_name, property_value);
	}

	// get current jsbsim state
	virtual	void updateKinematicState()
	{
		current_state_.pose.position = position_;
		current_state_.pose.orientation = orientation_;
		current_state_.twist.linear = linear_velocity_;
		current_state_.twist.angular = angular_velocity_;
		current_state_.accelerations.linear = linear_acceleration_;
		current_state_.accelerations.angular = angular_acceleration_;
	}

	// gets the effective control deflection in radians
	virtual void updateControlState()
	{
		const float aileron = getProperty("fcs/effective-aileron-pos");
		const float elevator = getProperty("fcs/elevator-pos-rad");
		const float throttle = getProperty("propulsion/engine/thrust-lbs"); // pounds of thrust engine is producing
		const float rudder = getProperty("fcs/rudder-pos-norm");
		jsbsim_control_state_ = { aileron, elevator, throttle, rudder };
	}
	
	// get the distance from the earth zero point in metres
	void getPositionMeters()
	{
		const float lat_deg = getProperty("position/lat-geod-deg");
		const float long_deg = getProperty("position/long-gc-deg");
		const float altitude_m = getProperty("position/h-sl-ft") / 3.28;
		const float lat_m = 111320 * lat_deg;
		const float long_m = 40075000 * long_deg * cos(lat_deg * (pi / 180.0)) / 360.0;
		position_ = { lat_m, long_m, - altitude_m };
	}

	// get the aircraft's orientation in radians
	void getOrientationRadians()
	{
		const float pitch = getProperty("attitude/pitch-rad");
		const float roll = getProperty("attitude/roll-rad");
		const float yaw = getProperty("attitude/psi-deg") * (pi / 180.0);
		Vector3r euler_orientation = { pitch, roll, yaw };
		orientation_ = VectorMath::toQuaternion(euler_orientation[0],
			euler_orientation[1],
			euler_orientation[2]);
	}

	void getLinearVelocity()
	{
		const float north_vel = getProperty("velocities/v-north-fps") / 3.28;
		const float east_vel = getProperty("velocities/v-east-fps") / 3.28;
		const float vertical_vel = getProperty("velocities/v-down-fps") / 3.28;
		linear_velocity_ = { north_vel, east_vel, -vertical_vel };
	}

	void getAngularVelocity()
	{
		const float p = getProperty("velocities/p-rad_sec");
		const float q = getProperty("velocities/q-rad_sec");
		const float r = getProperty("velocities/r-rad_sec");
		angular_velocity_ = { p, q, r };
	}

	void getLinearAccel()
	{
		const float ax = getProperty("accelerations/udot-ft_sec2") / 3.28;
		const float ay = getProperty("accelerations/vdot-ft_sec2") / 3.28;
		const float az = getProperty("accelerations/wdot-ft_sec2") / 3.28;
		linear_acceleration_ = { ax, ay, az };
	}

	void getAngularAccel()
	{
		const float pdot = getProperty("accelerations/pdot-rad_sec2");
		const float qdot = getProperty("accelerations/qdot-rad_sec2");
		const float rdot = getProperty("accelerations/rdot-rad_sec2");
		angular_acceleration_ = {pdot, qdot, rdot};
	}
	
private:

	CollisionInfo collision_info_;
	JSBSim::FGFDMExec jsbsim_aircraft;
	Kinematics::State current_state_;
	vector<double> jsbsim_control_state_ = { 0, 0, 0, 0 };
	Vector3r position_;
	Quaternionr orientation_;
	Vector3r linear_velocity_;
	Vector3r angular_velocity_;
	Vector3r linear_acceleration_;
	Vector3r angular_acceleration_;
	CollisionResponse collision_response_;
	std::string model_path_;
	double pi = 3.1415926535897932;
	double delta_t_ = 0.0021; // set the simulation update rate, defaults to 480Hz
	
};
}
}
#endif