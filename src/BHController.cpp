#include "BHController.h"

BHController::BHController(OpenRAVE::EnvironmentBasePtr env, std::string const &ns)
    : OpenRAVE::ControllerBase(env)
    , bhd_ns_(ns)
{
}

bool BHController::Init(OpenRAVE::RobotBasePtr robot, std::vector<int> const &dof_indices, int ctrl_transform)
{
    BOOST_ASSERT(robot && ctrl_transform == 0);
    BOOST_ASSERT(dof_indices.size() == 4);
    robot_ = robot;

    nh_.setCallbackQueue(&queue_);
    ros::NodeHandle nh_bhd(nh_, bhd_ns_);

    dof_indices_ = dof_indices;
    sub_bhstate_ = nh_bhd.subscribe("handstate", 1, &BHController::bhstateCallback, this);
    srv_move_ = nh_bhd.serviceClient<owd_msgs::MoveHand>("MoveHand");
    srv_reset_ = nh_bhd.serviceClient<owd_msgs::ResetHand>("ResetHand");
    return true;
}

void BHController::SimulationStep(OpenRAVE::dReal time_ellapsed)
{
    queue_.callAvailable();

    // Update the DOF values from the most recent WAMState message.
    if (current_bhstate_) {
        std::vector<OpenRAVE::dReal> dof_values;
        robot_->GetDOFValues(dof_values);

        for (size_t index = 0; index < dof_indices_.size(); ++index) {
            size_t const dof_index = dof_indices_[index];
            BOOST_ASSERT(dof_index < dof_values.size());
            dof_values[dof_index] = current_bhstate_->positions[index];
        }

        robot_->SetDOFValues(dof_values);
    }
}

void BHController::Reset(int options)
{
    current_bhstate_ = owd_msgs::BHState::ConstPtr();

    owd_msgs::ResetHand::Request request;
    owd_msgs::ResetHand::Response response;
    bool const success = srv_reset_.call(request, response) && response.ok;
    if (!success && !response.reason.empty()) {
        RAVELOG_ERROR("Resetting hand failed: %s\n", response.reason.c_str());
    } else if (!success) {
        RAVELOG_ERROR("Resetting hand failed.\n");
    }
}

bool BHController::IsDone(void)
{
    return !current_bhstate_ || current_bhstate_->state == owd_msgs::BHState::state_done;
}

OpenRAVE::RobotBasePtr BHController::GetRobot(void) const
{
    return robot_;
}

std::vector<int> const &BHController::GetControlDOFIndices(void) const
{
    return dof_indices_;
}

int BHController::IsControlTransformation(void) const
{
    return 0;
}

bool BHController::SetDesired(std::vector<OpenRAVE::dReal> const &values,
                               OpenRAVE::TransformConstPtr transform)
{
    BOOST_ASSERT(values.size() == dof_indices_.size() && !transform);
    size_t const num_dofs = dof_indices_.size();

    owd_msgs::MoveHand::Request request;
    request.movetype = owd_msgs::MoveHand::Request::movetype_position;
    request.positions.resize(num_dofs);

    for (size_t i = 0; i < num_dofs; ++i) {
        request.positions[i] = values[i];
    }

    owd_msgs::MoveHand::Response response;
    bool const success = srv_move_.call(request, response) && response.ok;
    if (!success && !response.reason.empty()) {
        RAVELOG_ERROR("Moving hand failed with error: %s\n", response.reason.c_str());
        return false;
    } else if (!success) {
        RAVELOG_ERROR("Moving hand failed with unknown error.\n");
        return false;
    }
    return true;
}

bool BHController::SetPath(OpenRAVE::TrajectoryBaseConstPtr traj)
{
    // TODO: Only warn if the trajectory contains the finger DOFs.
    RAVELOG_WARN("BHController does not support SetPath.\n");
    return true;
}

void BHController::bhstateCallback(owd_msgs::BHState::ConstPtr const &new_bhstate)
{
    // Verify that we received the WAMState messages in sequential order.
    if (current_bhstate_ && new_bhstate->header.stamp < current_bhstate_->header.stamp) {
        RAVELOG_WARN("Received BHState message with an out-of-order timestamp.\n");
        current_bhstate_ = owd_msgs::BHState::ConstPtr();
        return;
    }
    // Verify that the message contains the correct number of DOFs.
    else if (new_bhstate->positions.size() != dof_indices_.size()) {
        RAVELOG_WARN("Received BHState message with %d DOFs; expected %d.\n",
            static_cast<int>(new_bhstate->positions.size()),
            static_cast<int>(dof_indices_.size())
        );
        return;
    }
    current_bhstate_ = new_bhstate;
}