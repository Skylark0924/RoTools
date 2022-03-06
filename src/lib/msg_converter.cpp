//
// Created by dongzhipeng on 12/12/21.
//

#include "roport/msg_converter.h"

namespace roport {

MsgConverter::MsgConverter(const ros::NodeHandle& node_handle, const ros::NodeHandle& pnh)
    : nh_(node_handle), pnh_(pnh) {
  if (!init()) {
    return;
  }
  starts_.resize(enable_smooth_start_flags_.size());
}

auto MsgConverter::init() -> bool {
  XmlRpc::XmlRpcValue source_joint_groups;
  if (!getParam("source_joint_groups", source_joint_groups)) {
    return false;
  }
  ROS_ASSERT(source_joint_groups.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue target_joint_groups;
  if (!getParam("target_joint_groups", target_joint_groups)) {
    return false;
  }
  ROS_ASSERT(target_joint_groups.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue source_js_topics;
  if (!getParam("source_js_topics", source_js_topics)) {
    return false;
  }
  ROS_ASSERT(source_js_topics.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue target_js_topics;
  if (!getParam("target_js_topics", target_js_topics)) {
    return false;
  }
  ROS_ASSERT(target_js_topics.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue target_types;
  if (!getParam("target_types", target_types)) {
    return false;
  }
  ROS_ASSERT(target_types.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue target_args;
  if (!getParam("target_args", target_args)) {
    return false;
  }
  ROS_ASSERT(target_args.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue enable_smooth_start;
  if (!getParam("enable_smooth_start", enable_smooth_start)) {
    return false;
  }
  ROS_ASSERT(enable_smooth_start.getType() == XmlRpc::XmlRpcValue::TypeArray);

  XmlRpc::XmlRpcValue start_ref_topics;
  if (!getParam("start_ref_topics", start_ref_topics)) {
    return false;
  }
  ROS_ASSERT(start_ref_topics.getType() == XmlRpc::XmlRpcValue::TypeArray);

  ROS_ASSERT(source_joint_groups.size() > 0);
  ROS_ASSERT(source_joint_groups.size() == target_joint_groups.size() &&
             source_joint_groups.size() == source_js_topics.size() &&
             source_joint_groups.size() == target_js_topics.size() &&
             source_joint_groups.size() == target_types.size() && source_joint_groups.size() == target_args.size() &&
             source_joint_groups.size() == enable_smooth_start.size() &&
             source_joint_groups.size() == start_ref_topics.size());

  // Subscribe source JointState topics and publish them to target topics.
  // If enable_reflex_ is set, the values to be published will first be smoothed.
  for (int group_id = 0; group_id < source_js_topics.size(); group_id++) {
    ROS_ASSERT(source_js_topics[group_id].getType() == XmlRpc::XmlRpcValue::TypeString);
    ROS_ASSERT(target_js_topics[group_id].getType() == XmlRpc::XmlRpcValue::TypeString);
    ROS_ASSERT(source_joint_groups[group_id].getType() == XmlRpc::XmlRpcValue::TypeArray);
    ROS_ASSERT(target_joint_groups[group_id].getType() == XmlRpc::XmlRpcValue::TypeArray);
    ROS_ASSERT(source_joint_groups[group_id].size() == target_joint_groups[group_id].size());

    std::vector<std::string> source_names;
    source_names.reserve(source_joint_groups[group_id].size());
    for (int j = 0; j < source_joint_groups[group_id].size(); ++j) {
      source_names.push_back(source_joint_groups[group_id][j]);
    }
    std::vector<std::string> target_names;
    target_names.reserve(target_joint_groups[group_id].size());
    for (int j = 0; j < target_joint_groups[group_id].size(); ++j) {
      target_names.push_back(target_joint_groups[group_id][j]);
    }

    auto target_type = std::string(target_types[group_id]);
    auto target_arg = int(target_args[group_id]) >= 0 ? int(target_args[group_id]) : -1;
    auto smooth_start_flag = int(enable_smooth_start[group_id]) > 0 ? int(enable_smooth_start[group_id]) : 0;

    std::vector<double> max_vel;
    std::vector<double> max_acc;
    std::vector<double> max_jerk;
    if (smooth_start_flag > 0) {
      if (!phaseJointParameterMap<double>("max_vel", source_names, target_names, max_vel)) {
        return false;
      }
      if (!phaseJointParameterMap<double>("max_acc", source_names, target_names, max_acc)) {
        return false;
      }
      if (!phaseJointParameterMap<double>("max_jerk", source_names, target_names, max_jerk)) {
        return false;
      }
      ROS_ASSERT(!max_vel.empty() && max_vel.size() == max_acc.size() && max_vel.size() == max_jerk.size());
    }

    rotools::RuckigOptimizer* optimizer;
    if (smooth_start_flag > 0) {
      enable_smooth_start_flags_.push_back(true);
      auto subscriber = nh_.subscribe<sensor_msgs::JointState>(
          start_ref_topics[group_id], 1, [this, group_id, source_names](auto&& ph1) {
            return smoothStartCb(std::forward<decltype(ph1)>(ph1), group_id, source_names);
          });
      start_ref_subscribers_.push_back(subscriber);
      finished_smooth_start_flags_.push_back(false);
      optimizer = new rotools::RuckigOptimizer(static_cast<int>(source_names.size()), max_vel, max_acc, max_jerk);
    } else {
      enable_smooth_start_flags_.push_back(false);
      ros::Subscriber dummy_subscriber;
      start_ref_subscribers_.push_back(dummy_subscriber);
      finished_smooth_start_flags_.push_back(true);
    }
    optimizers_.push_back(optimizer);

    ros::Publisher publisher;
    if (target_type == msg_type_map_[kSensorMsgsJointState]) {
      publisher = nh_.advertise<sensor_msgs::JointState>(target_js_topics[group_id], 1);
    } else if (target_type == msg_type_map_[kFrankaCoreMsgsJointCommand]) {
#ifdef FRANKA_CORE_MSGS
      publisher = nh.advertise<franka_core_msgs::JointCommand>(target_js_topics_[group_id], 1);
#else
      ROS_ERROR_STREAM(prefix << "Request target topic of type: " << target_type
                              << ", but the source code is not compiled with this message definition");
      continue;
#endif
    } else if (target_type == msg_type_map_[kUbtCoreMsgsJointCommand]) {
#ifdef UBT_CORE_MSGS
      publisher = nh_.advertise<ubt_core_msgs::JointCommand>(target_js_topics[group_id], 1);
#else
      ROS_ERROR_STREAM(prefix << "Request target topic of type: " << target_type
                              << ", but the source code is not compiled with this message definition");
      continue;
#endif
    } else {
      ROS_ERROR_STREAM(prefix << "Unknown target topic type: " << target_type);
      continue;
    }

    ros::Subscriber subscriber = nh_.subscribe<sensor_msgs::JointState>(
        source_js_topics[group_id], 1,
        [this, group_id, publisher, target_type, target_arg, source_names, target_names](auto&& ph1) {
          return jointStateCb(std::forward<decltype(ph1)>(ph1), group_id, publisher, target_type, target_arg,
                              source_names, target_names);
        });
    publishers_.push_back(publisher);
    subscribers_.push_back(subscriber);
  }
  return true;
}

void MsgConverter::jointStateCb(const sensor_msgs::JointState::ConstPtr& msg,
                                const size_t& group_id,
                                const ros::Publisher& publisher,
                                const std::string& type,
                                const int& arg,
                                const std::vector<std::string>& source_names,
                                const std::vector<std::string>& target_names) {
  sensor_msgs::JointState filtered_msg;
  if (!filterJointState(msg, filtered_msg, source_names)) {
    return;
  }

  sensor_msgs::JointState smoothed_msg;
  if (enable_smooth_start_flags_[group_id] && !finished_smooth_start_flags_[group_id]) {
    if (!smoothJointState(filtered_msg, optimizers_[group_id], smoothed_msg)) {
      return;
    }
  } else {
    smoothed_msg = filtered_msg;
  }

  if (type == msg_type_map_[kSensorMsgsJointState]) {
    publishJointState(smoothed_msg, publisher, source_names, target_names);
  } else if (type == msg_type_map_[kFrankaCoreMsgsJointCommand]) {
    publishFrankaJointCommand(smoothed_msg, publisher, arg, source_names, target_names);
  } else if (type == msg_type_map_[kUbtCoreMsgsJointCommand]) {
    publishUBTJointCommand(smoothed_msg, publisher, arg, source_names, target_names);
  } else {
    ROS_ERROR_STREAM_ONCE(prefix << "Unknown target topic type: " << type);
  }
}

bool MsgConverter::filterJointState(const sensor_msgs::JointState::ConstPtr& src_msg,
                                    sensor_msgs::JointState& filtered_msg,
                                    const std::vector<std::string>& source_names) {
  if (src_msg->position.empty()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source JointState message defines no position");
    return false;
  }
  if (src_msg->position.size() < source_names.size()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source JointState message have fewer positions ("
                                        << src_msg->position.size() << ") than expected (" << source_names.size()
                                        << ")");
    return false;
  }

  filtered_msg.header = src_msg->header;
  filtered_msg.name = source_names;

  size_t i = 0;
  for (const auto& name : source_names) {
    auto result = findInVector(src_msg->name, name);
    if (result.first) {
      filtered_msg.position.push_back(src_msg->position[result.second]);
      if (src_msg->velocity.size() == src_msg->position.size()) {
        filtered_msg.velocity.push_back(src_msg->velocity[result.second]);
      }
      if (src_msg->effort.size() == src_msg->position.size()) {
        filtered_msg.effort.push_back(src_msg->effort[result.second]);
      }
    } else {
      ROS_WARN_STREAM_ONCE(
          prefix << "No name in the source joint state message match the given source names (print only once)");
      if (src_msg->position.size() == source_names.size()) {
        filtered_msg.position.push_back(src_msg->position[i]);
        if (src_msg->velocity.size() == source_names.size()) {
          filtered_msg.velocity.push_back(src_msg->velocity[i]);
        }
        if (src_msg->effort.size() == source_names.size()) {
          filtered_msg.effort.push_back(src_msg->effort[i]);
        }
      } else {
        ROS_ERROR_STREAM(prefix << "Source joint state message defines no " << name);
        return false;
      }
    }
    i++;
  }
  return true;
}

auto MsgConverter::smoothJointState(const sensor_msgs::JointState& msg,
                                    rotools::RuckigOptimizer* oto,
                                    sensor_msgs::JointState& smoothed_msg) -> bool {
  oto->setTargetState(msg);
  if (!oto->isInitialStateSet()) {
    return false;
  }

  smoothed_msg.header = msg.header;
  smoothed_msg.name = msg.name;

  std::vector<double> q_cmd;
  std::vector<double> dq_cmd;
  oto->update(q_cmd, dq_cmd);
  smoothed_msg.position = q_cmd;
  smoothed_msg.velocity = dq_cmd;
  smoothed_msg.effort = msg.effort;  // effort is not tackled
  return true;
}

void MsgConverter::smoothStartCb(const sensor_msgs::JointState::ConstPtr& msg,
                                 const int& group_id,
                                 const std::vector<std::string>& source_names) {
  if (finished_smooth_start_flags_[group_id] || !optimizers_[group_id]->isTargetStateSet()) {
    return;
  }

  sensor_msgs::JointState filtered_msg;
  if (!filterJointState(msg, filtered_msg, source_names)) {
    return;
  }

  if (!optimizers_[group_id]->isInitialStateSet()) {
    optimizers_[group_id]->setInitialState(filtered_msg);
    starts_[group_id] = std::chrono::steady_clock::now();
    return;
  }

  std::vector<double> q_desired;
  optimizers_[group_id]->getTargetPosition(q_desired);
  size_t violated_i;
  double residual;
  if (allClose<double>(filtered_msg.position, q_desired, violated_i, residual)) {
    finished_smooth_start_flags_[group_id] = true;
    ROS_INFO("Successfully moved group %d to the start position.", group_id);
  } else {
    ROS_WARN_THROTTLE(1, "Smoothly moving group %d to the start position ...", group_id);
  }

  if (std::chrono::steady_clock::now() - starts_[group_id] > std::chrono::seconds(20)) {
    finished_smooth_start_flags_[group_id] = true;
    ROS_WARN("Unable to move group %d to the start position in 20 sec due to joint #%zu: %s. Residual %f. Aborted.",
             group_id, violated_i, source_names[violated_i].c_str(), residual);
  }
}

template <class T>
bool MsgConverter::phaseJointParameterMap(const std::string& param_name,
                                          const std::vector<std::string>& source_names,
                                          const std::vector<std::string>& target_names,
                                          std::vector<T>& param_out) {
  std::map<std::string, T> param_map;
  if (!getParam(param_name, param_map)) {
    return false;
  }
  for (size_t n = 0; n < source_names.size(); ++n) {
    if (param_map.find(source_names[n]) != param_map.end()) {
      param_out.push_back(param_map[source_names[n]]);
    } else if (param_map.find(target_names[n]) != param_map.end()) {
      param_out.push_back(param_map[target_names[n]]);
    } else {
      ROS_ERROR_STREAM(prefix << ("Unable to find %s param for %s(%s)", param_name.c_str(), source_names[n].c_str(),
                                  target_names[n].c_str()));
      return false;
    }
  }
  return true;
}

void MsgConverter::publishJointState(const sensor_msgs::JointState& src_msg,
                                     const ros::Publisher& pub,
                                     const std::vector<std::string>& source_names,
                                     const std::vector<std::string>& target_names) {
  sensor_msgs::JointState tgt_msg;
  tgt_msg.header = src_msg.header;

  if (src_msg.name.empty()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source JointState topic have empty name field");
    return;
  }
  // It is possible to convert only a part of the joint values in the given source message to target message.
  // i.e., src_msg.name.size() >= source_names.size() is legal
  if (src_msg.name.size() < source_names.size()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source message has fewer joint names (" << src_msg.name.size()
                                        << ") than expected (" << source_names.size() << ")");
    return;
  }
  for (size_t i = 0; i < source_names.size(); ++i) {
    auto result = findInVector<std::string>(src_msg.name, source_names[i]);
    if (result.first) {
      tgt_msg.name.push_back(target_names[i]);
      tgt_msg.position.push_back(src_msg.position[result.second]);
      if (src_msg.velocity.size() == src_msg.position.size()) {
        tgt_msg.velocity.push_back(src_msg.velocity[result.second]);
      }
      if (src_msg.effort.size() == src_msg.position.size()) {
        tgt_msg.effort.push_back(src_msg.effort[result.second]);
      }
    } else {
      ROS_ERROR_STREAM(prefix << "Source joint name " << source_names[i]
                              << "does not match any name in the given JointState message");
    }
  }
  pub.publish(tgt_msg);
}

void MsgConverter::publishFrankaJointCommand(const sensor_msgs::JointState& src_msg,
                                             const ros::Publisher& pub,
                                             const int& arg,
                                             const std::vector<std::string>& source_names,
                                             const std::vector<std::string>& target_names) {
#ifdef FRANKA_CORE_MSGS
  franka_core_msgs::JointCommand tgt_msg;
  tgt_msg.header = src_msg.header;
  std::set<int> supported_modes{tgt_msg.IMPEDANCE_MODE, tgt_msg.POSITION_MODE, tgt_msg.TORQUE_MODE,
                                tgt_msg.VELOCITY_MODE};
  if (supported_modes.find(arg) == supported_modes.end()) {
    ROS_ERROR_STREAM_ONCE(prefix << "Mode " << arg << " is not supported by franka_core_msg");
    return;
  }
  tgt_msg.mode = arg;

  if (src_msg.name.empty()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source JointState topic have empty name field");
    return;
  }
  // It is possible to convert only a part of the joint values in the given message.
  if (src_msg.name.size() < source_names.size()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Message name field has fewer names than source names");
    return;
  }
  for (size_t i = 0; i < source_names.size(); ++i) {
    auto result = findInVector<std::string>(src_msg.name, source_names[i]);
    if (result.first) {
      tgt_msg.names.push_back(target_names[i]);
      tgt_msg.position.push_back(src_msg.position[result.second]);
      if (src_msg.velocity.size() == src_msg.position.size()) {
        tgt_msg.velocity.push_back(src_msg.velocity[result.second]);
      }
      if (src_msg.effort.size() == src_msg.position.size()) {
        tgt_msg.effort.push_back(src_msg.effort[result.second]);
      }
    } else {
      ROS_ERROR_STREAM(prefix << "Source joint name " << source_names[i]
                              << "does not match any name in the given JointState message");
    }
  }
  pub.publish(tgt_msg);
#endif
}

void MsgConverter::publishUBTJointCommand(const sensor_msgs::JointState& src_msg,
                                          const ros::Publisher& pub,
                                          const int& arg,
                                          const std::vector<std::string>& source_names,
                                          const std::vector<std::string>& target_names) {
#ifdef UBT_CORE_MSGS
  ubt_core_msgs::JointCommand tgt_msg;
  const int kPositionModeID = 5;
  const int kOTGModeID = 8;
  std::set<int> supported_modes{kPositionModeID, kOTGModeID};
  if (supported_modes.find(arg) == supported_modes.end()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Mode " << arg << " is not supported by UBT msg");
    return;
  }
  tgt_msg.mode = arg;

  if (src_msg.name.empty()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source JointState topic have empty name field");
    return;
  }
  if (src_msg.name.size() < source_names.size()) {
    ROS_ERROR_STREAM_THROTTLE(3, prefix << "Source message has fewer joint names (" << src_msg.name.size()
                                        << ") than expected (" << source_names.size() << ")");
    return;
  }
  for (size_t i = 0; i < source_names.size(); ++i) {
    auto result = findInVector<std::string>(src_msg.name, source_names[i]);
    if (result.first) {
      tgt_msg.names.push_back(target_names[i]);
      tgt_msg.command.push_back(src_msg.position[result.second]);
    } else {
      ROS_ERROR_STREAM(prefix << "Source joint name " << source_names[i]
                              << "does not match any name in the given JointState message");
    }
  }
  pub.publish(tgt_msg);
#endif
}

}  // namespace roport