//
// Created by smart on 2021/9/6.
//

#ifndef SRC_ONLINE_TRAJECTORY_OPTIMIZER_H
#define SRC_ONLINE_TRAJECTORY_OPTIMIZER_H

#include "Reflexxes/ReflexxesAPI.h"
#include "Reflexxes/RMLPositionFlags.h"
#include "Reflexxes/RMLPositionInputParameters.h"
#include "Reflexxes/RMLPositionOutputParameters.h"

#include <stdlib.h>
#include <vector>
#include <iostream>

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/SVD>
#include <eigen3/Eigen/LU>
#include <eigen3/Eigen/QR>
#include <eigen3/Eigen/Geometry>
#include <eigen3/Eigen/Cholesky>

#include "ros/ros.h"

namespace rotools {

  class OnlineTrajectoryOptimizer
  {
  public:
    bool enabled_;
    bool initialized_;
    size_t dof_;
    double frequency_;

    ReflexxesAPI *reflex_;
    RMLPositionInputParameters *input_param_;
    RMLPositionOutputParameters *output_param_;
    RMLPositionFlags flags_;

    explicit OnlineTrajectoryOptimizer(int dof, double frequency=1000.);
    ~OnlineTrajectoryOptimizer();

    void setParameters(const std::vector<double>& max_vel, const std::vector<double>& max_acc,
                       const std::vector<double>& max_jerk, double vel_scale,
                       double acc_scale, double jerk_scale);
    void update(const std::vector<double>& q, const std::vector<double>& dq);
    bool output(const std::vector<double>& q_desired, const std::vector<double>& dq_desired,
                std::vector<double> &q_out, std::vector<double> &dq_out, std::vector<double> &ddq_out);
  };

}


#endif //SRC_ONLINE_TRAJECTORY_OPTIMIZER_H
