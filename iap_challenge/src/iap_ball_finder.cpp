/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ros/ros.h"
#include "pluginlib/class_list_macros.h"
#include "nodelet/nodelet.h"
#include <geometry_msgs/Twist.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/point_types_conversion.h>
#include "dynamic_reconfigure/server.h"
#include "iap_challenge/IapBallFinderConfig.h"

#include <visualization_msgs/Marker.h>


namespace iap_challenge
{
typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloud;

//* The IAP Challenge Ball Finder nodelet.
/**
 * The IAP Challenge Ball Finder nodelet. Subscribes to point clouds
 * from the 3dsensor, processes them, and publishes maker messages at
 * the ball's location.
 */
class IapBallFinder : public nodelet::Nodelet
{
public:
  /*!
   * @brief The constructor for the IAP Challenge Ball Finder nodelet.
   * Constructor for the IAP Challenge Ball Finder nodelet.
   */
  IapBallFinder() : min_y_(0.1), max_y_(0.5),
		    min_x_(-0.2), max_x_(0.2),
		    max_z_(0.8), hue_(0.8),
		    hue_distance_(0.2)
  {

  }

  ~IapBallFinder()
  {
    delete srv_;
  }

private:
  double min_y_; /**< The minimum y position of the points in the box. */
  double max_y_; /**< The maximum y position of the points in the box. */
  double min_x_; /**< The minimum x position of the points in the box. */
  double max_x_; /**< The maximum x position of the points in the box. */
  double max_z_; /**< The maximum z position of the points in the box. */
  double hue_; /**< Ball hue. */
  double hue_distance_; /**< Accepted distance from ball hue. */

  // Dynamic reconfigure server
  dynamic_reconfigure::Server<iap_challenge::IapBallFinderConfig>* srv_;

  /*!
   * @brief OnInit method from node handle.
   * OnInit method from node handle. Sets up the parameters
   * and topics.
   */
  virtual void onInit()
  {
    ros::NodeHandle& nh = getNodeHandle();
    ros::NodeHandle& private_nh = getPrivateNodeHandle();

    private_nh.getParam("min_y", min_y_);
    private_nh.getParam("max_y", max_y_);
    private_nh.getParam("min_x", min_x_);
    private_nh.getParam("max_x", max_x_);
    private_nh.getParam("max_z", max_z_);
    private_nh.getParam("hue", hue_);
    private_nh.getParam("hue_distance", hue_distance_);

    markerpub_ = private_nh.advertise<visualization_msgs::Marker>("marker",1);
    bboxpub_ = private_nh.advertise<visualization_msgs::Marker>("bbox",1);
    cloudpub_ = private_nh.advertise<PointCloud>("points",1);
    sub_= nh.subscribe<PointCloud>("depth_registered/points", 1, &IapBallFinder::cloudcb, this);

    srv_ = new dynamic_reconfigure::Server<iap_challenge::IapBallFinderConfig>(private_nh);
    dynamic_reconfigure::Server<iap_challenge::IapBallFinderConfig>::CallbackType f =
      boost::bind(&IapBallFinder::reconfigure, this, _1, _2);
    srv_->setCallback(f);
  }

  void reconfigure(iap_challenge::IapBallFinderConfig &config, uint32_t level)
  {
    min_y_ = config.min_y;
    max_y_ = config.max_y;
    min_x_ = config.min_x;
    max_x_ = config.max_x;
    max_z_ = config.max_z;
    hue_ = config.hue;
    hue_distance_ = config.hue_distance;
  }

  /*!
   * @brief Callback for point clouds.
   * Callback for point clouds. Find points in a bounding box with
   * color near hue_. Clusters points. Published centriod of clustered
   * points as a marker message.
   * @param cloud The point cloud message.
   */
  void cloudcb(const PointCloud::ConstPtr&  cloud)
  {
    //Point cloud of accepted points
    PointCloud incloud;
    incloud.header = cloud->header;

    //Iterate through all the points in the region and find the average of the position
    BOOST_FOREACH (const pcl::PointXYZRGB& pt, cloud->points)
    {
      //First, ensure that the point's position is valid. This must be done in a seperate
      //if because we do not want to perform comparison on a nan value.
      if (!std::isnan(pt.x) && !std::isnan(pt.y) && !std::isnan(pt.z))
      {
        //Test to ensure the point is within the aceptable box.
        if (-pt.y > min_y_ && -pt.y < max_y_ && pt.x < max_x_ && pt.x > min_x_ && pt.z < max_z_)
        {
	  //Convert to HSV space
	  pcl::PointXYZRGB pt_copy(pt);
	  pcl::PointXYZHSV pt_hsv;
	  pcl::PointXYZRGBtoXYZHSV(pt_copy, pt_hsv);

	  //Threshold on HSV
	  if (fabs(hue_ - pt_hsv.h) < hue_distance_/2 ||
	      fabs(hue_ - 360 + pt_hsv.h) < hue_distance_/2) {
	    incloud.push_back(pt);
	  }
        }
      }
    }

    cloudpub_.publish(incloud);
    publishBbox();
  }

  void publishMarker(double x,double y,double z)
  {
    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::SPHERE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.2;
    marker.scale.y = 0.2;
    marker.scale.z = 0.2;
    marker.color.a = 1.0;
    marker.color.r = 1.0;
    marker.color.g = 0.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    markerpub_.publish( marker );
  }

  void publishBbox()
  {
    double x = (min_x_ + max_x_)/2;
    double y = (min_y_ + max_y_)/2;
    double z = (0 + max_z_)/2;

    double scale_x = (max_x_ - x)*2;
    double scale_y = (max_y_ - y)*2;
    double scale_z = (max_z_ - z)*2;

    visualization_msgs::Marker marker;
    marker.header.frame_id = "/camera_rgb_optical_frame";
    marker.header.stamp = ros::Time();
    marker.ns = "my_namespace";
    marker.id = 1;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = x;
    marker.pose.position.y = -y;
    marker.pose.position.z = z;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = scale_x;
    marker.scale.y = scale_y;
    marker.scale.z = scale_z;
    marker.color.a = 0.5;
    marker.color.r = 0.0;
    marker.color.g = 1.0;
    marker.color.b = 0.0;
    //only if using a MESH_RESOURCE marker type:
    bboxpub_.publish( marker );
  }

  ros::Subscriber sub_;
  ros::Publisher markerpub_;
  ros::Publisher bboxpub_;
  ros::Publisher cloudpub_;
};

PLUGINLIB_DECLARE_CLASS(iap_challenge, IapBallFinder, iap_challenge::IapBallFinder, nodelet::Nodelet);

}
