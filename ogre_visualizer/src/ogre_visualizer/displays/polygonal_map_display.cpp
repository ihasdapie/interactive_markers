/*
 * Copyright (c) 2008, Willow Garage, Inc.
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
 *
 * $Id$
 *
 */

#include "polygonal_map_display.h"
#include "properties/property.h"
#include "properties/property_manager.h"
#include "common.h"

#include "ogre_tools/arrow.h"

#include <ros/node.h>
#include <tf/transform_listener.h>

#include <boost/bind.hpp>

#include <OgreSceneNode.h>
#include <OgreSceneManager.h>
#include <OgreManualObject.h>
#include <OgreBillboardSet.h>

#include <ogre_tools/point_cloud.h>

namespace ogre_vis
{

  PolygonalMapDisplay::PolygonalMapDisplay (const std::string & name, VisualizationManager * manager) 
    : Display (name, manager)
    , color_ (0.1f, 1.0f, 0.0f)
    , render_operation_ (polygon_render_ops::PLines)
    , override_color_ (false)
    , new_message_ (false)
    , color_property_ (NULL)
    , topic_property_ (NULL)
    , override_color_property_ (NULL)
    , render_operation_property_ (NULL)
    , point_size_property_ (NULL)
    , z_position_property_ (NULL)
    , alpha_property_ (NULL)
  {
    scene_node_ = scene_manager_->getRootSceneNode ()->createChildSceneNode ();

    static int count = 0;
    std::stringstream ss;
    ss << "Polygonal Map" << count++;
    manual_object_ = scene_manager_->createManualObject (ss.str ());
    manual_object_->setDynamic (true);
    scene_node_->attachObject (manual_object_);

    cloud_ = new ogre_tools::PointCloud (scene_manager_, scene_node_);
    cloud_->setBillboardType (Ogre::BBT_PERPENDICULAR_COMMON);
    setAlpha (1.0f);
    setPointSize (0.05f);
    setZPosition (0.0f);
  }

  PolygonalMapDisplay::~PolygonalMapDisplay ()
  {
    unsubscribe ();
    clear ();

    scene_manager_->destroyManualObject (manual_object_);

    delete cloud_;
  }

  void
    PolygonalMapDisplay::clear ()
  {
    manual_object_->clear ();
    cloud_->clear ();
  }

  void
    PolygonalMapDisplay::setTopic (const std::string & topic)
  {
    unsubscribe ();
    topic_ = topic;
    subscribe ();

    if (topic_property_)
      topic_property_->changed ();

    causeRender ();
  }

  void
    PolygonalMapDisplay::setColor (const Color & color)
  {
    color_ = color;

    if (color_property_)
      color_property_->changed ();

    processMessage ();
    causeRender ();
  }

  void
    PolygonalMapDisplay::setOverrideColor (bool override)
  {
    override_color_ = override;

    if (override_color_property_)
      override_color_property_->changed ();

    processMessage ();
    causeRender ();
  }

  void
    PolygonalMapDisplay::setRenderOperation (int op)
  {
    render_operation_ = op;

    if (render_operation_property_)
      render_operation_property_->changed ();

    processMessage ();
    causeRender ();
  }

  void
    PolygonalMapDisplay::setPointSize (float size)
  {
    point_size_ = size;

    if (point_size_property_)
      point_size_property_->changed ();

    cloud_->setBillboardDimensions (size, size);
    causeRender ();
  }

  void
    PolygonalMapDisplay::setZPosition (float z)
  {
    z_position_ = z;

    if (z_position_property_)
      z_position_property_->changed ();

    scene_node_->setPosition (0.0f, z, 0.0f);
    causeRender ();
  }

  void
    PolygonalMapDisplay::setAlpha (float alpha)
  {
    alpha_ = alpha;
    cloud_->setAlpha (alpha);

    if (alpha_property_)
      alpha_property_->changed ();

    processMessage ();
    causeRender ();
  }

  void
    PolygonalMapDisplay::subscribe ()
  {
    if (!isEnabled ())
      return;

    if (!topic_.empty ())
      ros_node_->subscribe (topic_, message_, &PolygonalMapDisplay::incomingMessage, this, 1);
  }

  void
    PolygonalMapDisplay::unsubscribe ()
  {
    if (!topic_.empty ())
      ros_node_->unsubscribe (topic_, &PolygonalMapDisplay::incomingMessage, this);
  }

  void
    PolygonalMapDisplay::onEnable ()
  {
    scene_node_->setVisible (true);
    subscribe ();
  }

  void
    PolygonalMapDisplay::onDisable ()
  {
    unsubscribe ();
    clear ();
    scene_node_->setVisible (false);
  }

  void
    PolygonalMapDisplay::fixedFrameChanged ()
  {
    clear ();
  }

  void
    PolygonalMapDisplay::update (float dt)
  {
    if (new_message_)
    {
      processMessage ();
      new_message_ = false;
      causeRender ();
    }
  }

  void
    PolygonalMapDisplay::processMessage ()
  {
    message_.lock ();
    clear ();
    tf::Stamped < tf::Pose > pose (btTransform (btQuaternion (0.0f, 0.0f, 0.0f), btVector3 (0.0f, 0.0f, z_position_)), ros::Time (), "map");

    if (tf_->canTransform (fixed_frame_, "map", ros::Time ()))
    {
      try
      {
        tf_->transformPose (fixed_frame_, pose, pose);
      }
      catch (tf::TransformException & e)
      {
        ROS_ERROR ("Error transforming from frame 'map' to frame '%s'\n", fixed_frame_.c_str ());
      }
    }

    Ogre::Vector3 position = Ogre::Vector3 (pose.getOrigin ().x (), pose.getOrigin ().y (), pose.getOrigin ().z ());
    robotToOgre (position);

    btScalar yaw, pitch, roll;
    pose.getBasis ().getEulerZYX (yaw, pitch, roll);

    Ogre::Matrix3 orientation (ogreMatrixFromRobotEulers( yaw, pitch, roll));

    /*btQuaternion quat;
    pose.getBasis ().getRotation (quat);
    Ogre::Quaternion orientation (Ogre::Quaternion::IDENTITY);
    ogreToRobot (orientation);
    orientation = Ogre::Quaternion (quat.w (), quat.x (), quat.y (), quat.z ()) * orientation;
    robotToOgre (orientation);*/

    manual_object_->clear ();

    Ogre::ColourValue color;

    uint32_t num_polygons = message_.get_polygons_size ();
    uint32_t num_total_points = 0;
    for (uint32_t i = 0; i < num_polygons; i++)
      num_total_points += message_.polygons[i].points.size ();

    // If we render points, we don't care about the order
    if (render_operation_ == polygon_render_ops::PPoints)
    {
      typedef std::vector < ogre_tools::PointCloud::Point > V_Point;
      V_Point points;
      points.resize (num_total_points);
      uint32_t cnt_total_points = 0;
      for (uint32_t i = 0; i < num_polygons; i++)
      {
        for (uint32_t j = 0; j < message_.polygons[i].points.size (); j++)
        {
          ogre_tools::PointCloud::Point & current_point = points[cnt_total_points];

          current_point.x_ = message_.polygons[i].points[j].x;
          current_point.y_ = message_.polygons[i].points[j].y;
          current_point.z_ = message_.polygons[i].points[j].z;
          if (override_color_)
            color = Ogre::ColourValue (color_.r_, color_.g_, color_.b_, alpha_);
          else
            color = Ogre::ColourValue (message_.polygons[i].color.r, message_.polygons[i].color.g, message_.polygons[i].color.b, alpha_);
          current_point.r_ = color.r;
          current_point.g_ = color.g;
          current_point.b_ = color.b;
          cnt_total_points++;
        }
      }

      cloud_->clear ();

      if (!points.empty ())
        cloud_->addPoints (&points.front (), points.size ());
    }
    else
    {
      for (uint32_t i = 0; i < num_polygons; i++)
      {
        manual_object_->estimateVertexCount (message_.polygons[i].points.size ());
        manual_object_->begin ("BaseWhiteNoLighting", Ogre::RenderOperation::OT_LINE_STRIP);
        for (uint32_t j = 0; j < message_.polygons[i].points.size (); j++)
        {
          manual_object_->position (message_.polygons[i].points[j].x, 
                                    message_.polygons[i].points[j].y,
                                    message_.polygons[i].points[j].z);
          if (override_color_)
            color = Ogre::ColourValue (color_.r_, color_.g_, color_.b_, alpha_);
          else
            color = Ogre::ColourValue (message_.polygons[i].color.r, message_.polygons[i].color.g, message_.polygons[i].color.b, alpha_);
          manual_object_->colour (color);
        }
        manual_object_->end ();
      }
    }

    scene_node_->setPosition (position);
    scene_node_->setOrientation (orientation);

    message_.unlock ();
  }

  void
    PolygonalMapDisplay::incomingMessage ()
  {
    new_message_ = true;
  }

  void
    PolygonalMapDisplay::reset ()
  {
    clear ();
  }

  void
    PolygonalMapDisplay::createProperties ()
  {
    override_color_property_ = property_manager_->createProperty<BoolProperty>("Override Color", property_prefix_,
                                                                               boost::bind (&PolygonalMapDisplay::getOverrideColor, this),
                                                                               boost::bind (&PolygonalMapDisplay::setOverrideColor, this, _1),
                                                                               parent_category_, this);
    color_property_ = property_manager_->createProperty<ColorProperty>("Color", property_prefix_,
                                                                       boost::bind (&PolygonalMapDisplay::getColor, this),
                                                                       boost::bind (&PolygonalMapDisplay::setColor, this, _1),
                                                                       parent_category_, this);
    render_operation_property_ = property_manager_->createProperty<EnumProperty>("Render Operation", property_prefix_,
                                                                                 boost::bind (&PolygonalMapDisplay::getRenderOperation, this),
                                                                                 boost::bind (&PolygonalMapDisplay::setRenderOperation, this, _1),
                                                                                 parent_category_, this);
    render_operation_property_->addOption ("Lines", polygon_render_ops::PLines);
    render_operation_property_->addOption ("Points", polygon_render_ops::PPoints);

    z_position_property_ = property_manager_->createProperty<FloatProperty>("Z Position", property_prefix_, 
                                                                            boost::bind (&PolygonalMapDisplay::getZPosition, this),
                                                                            boost::bind (&PolygonalMapDisplay::setZPosition, this, _1),
                                                                            parent_category_, this);
    alpha_property_ = property_manager_->createProperty<FloatProperty>("Alpha", property_prefix_,
                                                                       boost::bind (&PolygonalMapDisplay::getAlpha, this),
                                                                       boost::bind (&PolygonalMapDisplay::setAlpha, this, _1),
                                                                       parent_category_, this);
    topic_property_ = property_manager_->createProperty<ROSTopicStringProperty>("Topic", property_prefix_,
                                                                                boost::bind (&PolygonalMapDisplay::getTopic, this),
                                                                                boost::bind (&PolygonalMapDisplay::setTopic, this, _1),
                                                                                parent_category_, this);
  }

  const char*
    PolygonalMapDisplay::getDescription ()
  {
    return ("Displays data from a std_msgs::PolygonalMap message as either points or lines.");
  }

} // namespace ogre_vis