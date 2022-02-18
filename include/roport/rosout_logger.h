#ifndef SRC_ROSOUT_LOGGER_H
#define SRC_ROSOUT_LOGGER_H

#include <behaviortree_cpp_v3/loggers/abstract_logger.h>
#include <behaviortree_cpp_v3/loggers/bt_zmq_publisher.h>
#include <ros/console.h>

namespace BT {
class RosoutLogger : public StatusChangeLogger {
  static std::atomic<bool> ref_count;

 public:
  explicit RosoutLogger(TreeNode* root_node, ros::console::Level verbosity_level = ros::console::Level::Info);

  [[nodiscard]] auto getLevel() const -> ros::console::Level;

  // Accepts only Info and Debug
  void setLevel(ros::console::Level level);

  ~RosoutLogger() override;

  void callback(Duration timestamp, const TreeNode& node, NodeStatus prev_status, NodeStatus status) override;

  void flush() override;

 private:
  ros::console::Level level_;
};

}  // namespace BT

#endif  // SRC_ROSOUT_LOGGER_H
