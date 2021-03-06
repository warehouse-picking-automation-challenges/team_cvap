#include "ros/ros.h"
#include <sensor_msgs/point_cloud_conversion.h>
#include "sensor_msgs/LaserScan.h"
#include "pcl_ros/impl/transforms.hpp"
#include <pcl/filters/passthrough.h>
#include <pcl/filters/voxel_grid.h>

#include <tf/transform_broadcaster.h>
#include <tf_conversions/tf_eigen.h>

//////////////// pcl mesh ////////////////////////
#include <pcl/PolygonMesh.h>
#include <pcl/io/vtk_lib_io.h>
#include <pcl/io/impl/vtk_lib_io.hpp>

#include <pcl/common/transforms.h>
#include <pcl/registration/ndt.h>

#include <ros/package.h>

#include"pcl_icp_fuser.h"


using namespace std;

Pcl_Icp_Fuser icp_fuser_;

ros::Publisher  pub_laser, put_select, put_kinect;
geometry_msgs::TransformStamped odom_trans;
tf::TransformListener* tfListener = NULL;

pcl::PointCloud<pcl::PointXYZRGB>  shelf_frame;
Eigen::Matrix4f g_pairTransform = Eigen::Matrix4f::Identity ();
bool transform_ready = false;

pcl::PointCloud<pcl::PointXYZ> obj_pcd;
bool init = false;
int  compute_num = 0;

void publish(ros::Publisher pub, pcl::PointCloud<pcl::PointXYZRGB> cloud, int type = 2)
{
    sensor_msgs::PointCloud2 pointlcoud2;
    pcl::toROSMsg(cloud, pointlcoud2);

    if(type == 2)
    {

        pub.publish(pointlcoud2);
     //   cout << "2" << endl;
    }
    else
    {
        sensor_msgs::PointCloud pointlcoud;
        sensor_msgs::convertPointCloud2ToPointCloud(pointlcoud2, pointlcoud);

        pointlcoud.header = pointlcoud2.header;
        pub.publish(pointlcoud);
    }

}

void publish(ros::Publisher pub, pcl::PointCloud<pcl::PointXYZ> cloud)
{
    sensor_msgs::PointCloud2 pointlcoud2;
    pcl::toROSMsg(cloud, pointlcoud2);

    pub.publish(pointlcoud2);


}



pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_fuser(pcl::PointCloud<pcl::PointXYZ>::Ptr sourse, pcl::PointCloud<pcl::PointXYZ>::Ptr target)
{
    // cout << "fuser" << endl;
    pcl::PointCloud<pcl::PointXYZ>::Ptr output (new pcl::PointCloud<pcl::PointXYZ>());

    Eigen::Matrix4f pairTransform;

    cout << "src point num: " << sourse->points.size() << "  tg num: " << target->points.size() << endl;
    icp_fuser_.pairAlign(target, sourse, output, pairTransform, true);

    //transform current pair into the global transform
    pcl::transformPointCloud (*sourse, *output, pairTransform);
    //pcl::transformPointCloud (*target, *output, pairTransform);

//    Eigen::Matrix4f combi_trans = g_pairTransform*pairTransform;
//    std::cout << "Transformation Matrix:" << std::endl;
//    //std::cout << pairTransform.rotation() << std::endl;

//    Eigen::Affine3f trans, diff_trans;
//    trans = combi_trans;
//    diff_trans = pairTransform;

//    float roll,pitch,yaw, d_r, d_p, d_y;
//    pcl::getEulerAngles(trans,roll,pitch,yaw);
//    pcl::getEulerAngles(diff_trans,d_r,d_p,d_y);
//    std::cout << "roll : " << roll << " ,pitch : " << pitch << " ,yaw : " << yaw << std::endl;
//    std::cout << "d roll : " << d_r << " ,d pitch : " << d_p << " ,d yaw : " << d_y << " ,d z: " << trans(2,3) << std::endl;

//    if((abs(yaw) < 0.01 && abs(trans(2,3)) < 0.01) || transform_ready == false)
//    {
//        g_pairTransform = combi_trans;
//        transform_ready = true;
//    }
//    else
//        transform_ready = false;

 //   trans = g_pairTransform;
//    pcl::getEulerAngles(trans,roll,pitch,yaw);
//    std::cout << "g roll : " << roll << " , g pitch : " << pitch << " ,g yaw : " << yaw << std::endl;

    Eigen::Affine3f g_trans, cur_trans;
    g_trans = g_pairTransform;
    cur_trans = pairTransform;

    float roll,pitch,yaw, cur_r, cur_p, cur_y;
    pcl::getEulerAngles(g_trans,roll,pitch,yaw);
    pcl::getEulerAngles(cur_trans,cur_r,cur_p,cur_y);

    if(!transform_ready || (abs(cur_y) < abs(yaw)))
        g_pairTransform = pairTransform;

    transform_ready = true;
    return output;
}


void callback_kinect2(const sensor_msgs::PointCloud2ConstPtr &cloud_in)
{
    if(compute_num > 2)
    {
        return;
    }
    compute_num ++;

////////////////////////////////// transform ////////////////////////////////////////
    sensor_msgs::PointCloud2 cloud_bin, cloud_test;
    tf::StampedTransform kinect_to_shelf, kinect_to_base;

    tfListener->waitForTransform("shelf", cloud_in->header.frame_id, ros::Time::now(), ros::Duration(0.0));
    tfListener->lookupTransform("shelf", cloud_in->header.frame_id, ros::Time(0), kinect_to_shelf);

//    /////////////////////////////// use it to caldulate the transform from base to shelf /////////////////////////
//    tfListener->waitForTransform("base", cloud_in->header.frame_id, ros::Time::now(), ros::Duration(0.0));
//    tfListener->lookupTransform("base", cloud_in->header.frame_id, ros::Time(0), kinect_to_base);
//    Eigen::Matrix4f eigen_transform_tobase;
//    pcl_ros::transformAsMatrix (kinect_to_base, eigen_transform_tobase);
//    //////////////////////////////////////////////////////////////////////////////////////////////////////////////

    std::cout << cloud_in->header.frame_id << endl;
    Eigen::Matrix4f eigen_transform_toshelf;
    pcl_ros::transformAsMatrix (kinect_to_shelf, eigen_transform_toshelf);
    pcl_ros::transformPointCloud (eigen_transform_toshelf, *cloud_in, cloud_bin);

    cloud_bin.header.frame_id = "shelf";
    pcl::PointCloud<pcl::PointXYZ> pcl_cloud_bin;
    pcl::fromROSMsg(cloud_bin, pcl_cloud_bin);

    /////////////////////////  kinect cloud  /////////////////////////////////////////
    // rotate kinect cloud based on previous matching result
    float crop_z = -0.2;
//    if(transform_ready)
//    {
 //       pcl::transformPointCloud (obj_pcd, obj_pcd, g_pairTransform);

//        Eigen::Matrix4f final_trans = g_pairTransform*eigen_transform_toshelf;
//        pcl_ros::transformPointCloud (final_trans, *cloud_in, cloud_test);
//        cloud_test.header.frame_id = "shelf";
//        put_kinect.publish(cloud_test);

     //   Eigen::Matrix4f final_trans = g_pairTransform*eigen_transform_toshelf;

//    }

    pcl::PointCloud<pcl::PointXYZ>::Ptr input_cloud      (new pcl::PointCloud<pcl::PointXYZ>(pcl_cloud_bin));
    pcl::PassThrough<pcl::PointXYZ> pass;
    pass.setInputCloud (input_cloud);
    pass.setFilterFieldName ("z");
    pass.setFilterLimits ( crop_z,  0.75);
    //pass.setFilterLimitsNegative (true);
    pass.filter (*input_cloud);

    pass.setInputCloud (input_cloud);
    pass.setFilterFieldName ("y");
    pass.setFilterLimits ( 0.6,  1.4);
    //pass.setFilterLimitsNegative (true);
    pass.filter (pcl_cloud_bin);

    // pcl::VoxelGrid<pcl::PointXYZ> sor;
    // sor.setInputCloud (input_cloud);
    // sor.setLeafSize (0.01f, 0.01f, 0.01f);
    // sor.filter (pcl_cloud);

    pcl::PointCloud<pcl::PointXYZ>::Ptr target      (new pcl::PointCloud<pcl::PointXYZ>(pcl_cloud_bin));
    pcl::PointCloud<pcl::PointXYZ>::Ptr source   (new pcl::PointCloud<pcl::PointXYZ>(obj_pcd));
    pcl::PointCloud<pcl::PointXYZ>::Ptr output   (new pcl::PointCloud<pcl::PointXYZ>());
    
    output = cloud_fuser(source, target);

    sensor_msgs::PointCloud2 pointlcoud2;
    pcl::toROSMsg(pcl_cloud_bin, pointlcoud2);
    //pcl::toROSMsg(pcl_cloud_bin, pointlcoud2);
    put_kinect.publish(pointlcoud2);

    sensor_msgs::PointCloud2 pointlcoud;
    pcl::toROSMsg(*output, pointlcoud);
    put_select.publish(pointlcoud);
}

void eigenMatrix4fToTransform(Eigen::Matrix4f &m, tf::Transform &t)
{
  tf::Matrix3x3 basis = tf::Matrix3x3(m(0,0), m(0,1), m(0,2),
                                  m(1,0), m(1,1), m(1,2),
                                  m(2,0), m(2,1), m(2,2));
  tf::Vector3   origin = tf::Vector3(m(0,3), m(1,3), m(2,3));
  t.setBasis(basis);
  t.setOrigin(origin);


}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "shelf_icpfuser");

    ros::NodeHandle node;
    ros::Rate rate(5.0);

    ros::Subscriber sub_kinect2 = node.subscribe<sensor_msgs::PointCloud2>("/kinect_chest/sd/points", 1, callback_kinect2);
  //  ros::Subscriber sub_depth_img = node.subscribe<sensor_msgs::Image>("/kinect2/qhd/image_depth", 1, depth_img_callback);

    put_select = node.advertise<sensor_msgs::PointCloud2>("/shelf_cloud", 1);
    put_kinect = node.advertise<sensor_msgs::PointCloud2>("/kinect_cloud", 1);

    tf::TransformBroadcaster broadcaster;
    tfListener = new (tf::TransformListener);

    std::string path = ros::package::getPath("apc_2016_mesh_models");
    string filename = path + "/shelf_model/shelf_icp.obj";

    pcl::PointCloud<pcl::PointXYZ> transformed_cloud;
    int count = 0;
    while (node.ok())
    {
        if( !init)
        {
            init = true;

            pcl::PolygonMesh::Ptr mesh(new pcl::PolygonMesh());
            if (pcl::io::loadPolygonFileOBJ(filename, *mesh) != -1)
            {   // PolygonMesh -> PointCloud<PointXYZRGB>
                pcl::fromPCLPointCloud2(mesh->cloud, obj_pcd);
            }

            Eigen::Affine3f transform_2 = Eigen::Affine3f::Identity();

            // The same rotation matrix as before; theta radians arround Z axis

            //transform_2.rotate (Eigen::AngleAxisf (M_PI, Eigen::Vector3f::UnitZ()));
         //   transform_2.rotate (Eigen::AngleAxisf (M_PI/9, Eigen::Vector3f::UnitX()));

            transform_2.translation() << 0.0, 0.3, 0.4;

            // Executing the transformation
            // You can either apply transform_1 or transform_2; they are the same
            pcl::transformPointCloud (obj_pcd, obj_pcd, transform_2);

            pcl::PointCloud<pcl::PointXYZ>::Ptr input_cloud      (new pcl::PointCloud<pcl::PointXYZ>(obj_pcd));
            pcl::PassThrough<pcl::PointXYZ> pass;
            pass.setInputCloud (input_cloud);
            pass.setFilterFieldName ("z");
            //pass.setFilterLimits ( 0.38,  0.5);
            pass.setFilterLimits ( -0.04,  0.6);
            //pass.setFilterLimitsNegative (true);
            pass.filter (*input_cloud);

            pass.setInputCloud (input_cloud);
            pass.setFilterFieldName ("y");
            pass.setFilterLimits ( 0.6,  1.4);
            //pass.setFilterLimitsNegative (true);
            pass.filter (obj_pcd);

            obj_pcd.header.frame_id = "shelf";
            publish(put_kinect, obj_pcd);
        }

        // cout << "pub transform shelf_icp" << endl;
        tf::Transform transform_tf;
        eigenMatrix4fToTransform(g_pairTransform, transform_tf);
        // std::cout << g_pairTransform << endl;
        broadcaster.sendTransform(tf::StampedTransform(transform_tf, ros::Time::now(), "shelf", "shelf_icp"));

        ros::spinOnce();
        rate.sleep();
    }
    return 0;
};
