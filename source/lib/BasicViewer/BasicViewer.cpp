//
// Created by LinKun on 10/2/15.
//

#include "BasicViewer/BasicViewer.h"
#include "BasicViewer/PointPairGL.h"

#include <iostream>
#include <algorithm>

#include <QResource>
#include <QThread>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>

#include <glm/gtc/matrix_transform.hpp>

#include <opencv2/opencv.hpp>

#include <Core/Utility.h>
#include <SLAM/CommonDefinitions.h>
#include <SLAM/Calibrator.h>
#include <Handler/ImageDataHandler.h>

namespace {
    using namespace std;
    using namespace glm;
}

namespace NiS {


    BasicViewer::BasicViewer(QWidget *parent) :
            QOpenGLWidget(parent),
            mode_(0),
            render_grid_(true),
            render_inliers_(true),
            render_point_cloud_(true),
            render_trajectory_(true),
            density_step_(5),
            scale_(1.0f),
            degree_(0.0f),
            spin_timer_(new QTimer(this)) {

        ui_.setupUi(this);

        connect(spin_timer_, SIGNAL(timeout()),
                this, SLOT(onSpin()));

        GL = new QOpenGLFunctions_4_1_Core;
    }

    BasicViewer::~BasicViewer() {

        delete GL;
    }

    void BasicViewer::initializeGL() {

        GL->initializeOpenGLFunctions();

        GL->glViewport(0, 0, width(), height());
        GL->glEnable(GL_DEPTH_TEST);
        GL->glEnable(GL_LINE_SMOOTH);
        GL->glEnable(GL_PROGRAM_POINT_SIZE);
        GL->glEnable(GL_CULL_FACE);

        background_color_ = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);

        shader_program_ = SetupShaderProgram(":/Shaders/Basic/BasicVertexShader.glsl",
                                             ":/Shaders/Basic/BasicFragmentShader.glsl", this);

        grid_ = new GridGL(GL, 0.0f, -1.0f, 0.0f, 30.0f, 1.0f);
        grid_->SetShaderProgram(shader_program_);
        grid_->SetupData();

    }

    void BasicViewer::paintGL() {

        using namespace glm;

        GL->glClearColor(background_color_.r, background_color_.g, background_color_.b, background_color_.a);
        GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        mat4 scale_matrix = scale(mat4(), vec3(scale_));
        mat4 translate_matrix = translate(mat4(), vec3(0.0f, 0.0f, 0.0f));
        mat4 rotation_matrix = rotate(mat4(), degree_, vec3(0.0f, 1.0f, 0.0f));
        mat4 model_matrix = translate_matrix * rotation_matrix * scale_matrix;

        model_matrix = model_matrix * model_translation_matrix_ * model_rotation_matrix_;

        mat4 view_matrix = camera_.ViewMatrix();

        mat4 projection_matrix = perspective(radians(45.0f),                                // radian
                                             (float) (this->width()) / this->height(), // aspect ratio
                                             0.001f,                                             // near z
                                             200.0f);                                          // far z

        mat4 transformation_matrix = projection_matrix * view_matrix * model_matrix;

        if (render_grid_) {
            grid_->SetTransformationMatrix(
                    projection_matrix * view_matrix * model_translation_matrix_ * model_rotation_matrix_);
            grid_->Render();
        }


        switch (mode_) {

            case 0: {

                glm::mat4 accumulated_transformation_matrix;

                if (render_point_cloud_) {
                    for (auto &keyframe_gl : keyframes_gl_) {
                        accumulated_transformation_matrix =
                                accumulated_transformation_matrix * keyframe_gl.GetAlignmentMatrix();
                        keyframe_gl.SetTransformationMatrix(transformation_matrix * accumulated_transformation_matrix);
                        keyframe_gl.Render();
                    }
                }

                if (render_trajectory_) {
                    for (auto &trajectory_gl : trajectory_gl_) {
                        trajectory_gl.SetTransformationMatrix(transformation_matrix);
                        trajectory_gl.Render();
                    }
                }

                for (auto &point_pair_gl : point_pair_gl_) {
                    point_pair_gl.SetTransformationMatrix(transformation_matrix);
                    point_pair_gl.Render();
                }

                break;
            }

            case 1: {

                glm::mat4 left_translation_mat = glm::translate(glm::mat4(), glm::vec3(-3.2f, 0.0f, 0.0f));
                glm::mat4 right_translation_mat = glm::translate(glm::mat4(), glm::vec3(+3.2f, 0.0f, 0.0f));
                glm::mat4 scale_mat = glm::scale(glm::mat4(), glm::vec3(1.5f));

                if (render_point_cloud_) {
                    for (auto &keyframe_for_inliers : keyframes_gl_for_inliers_) {

                        keyframe_for_inliers.SetTransformationMatrix(
                                transformation_matrix * left_translation_mat * scale_mat);
                        keyframe_for_inliers.Render();
                    }

                    for (auto &keyframe_for_inliers : keyframes_gl_for_inliers_) {

                        keyframe_for_inliers.SetTransformationMatrix(
                                transformation_matrix * right_translation_mat * scale_mat);
                        keyframe_for_inliers.Render();
                    }
                }

                for (auto &corresponding_points_pair_gl : corresponding_points_pair_gl_) {

                    corresponding_points_pair_gl.SetTransformationMatrix(
                            transformation_matrix * left_translation_mat * scale_mat);
                    corresponding_points_pair_gl.Render();
                }

                for (auto &inliers_pair_gl : inliers_pair_gl_) {

                    inliers_pair_gl.SetTransformationMatrix(transformation_matrix * right_translation_mat * scale_mat);
                    inliers_pair_gl.Render();
                }

                break;
            }

            default:
                break;

        }
    }

    void BasicViewer::mouseMoveEvent(QMouseEvent *e) {

        float dx = (e->x() - last_mouse_position_.x()) * 0.001f;
        float dy = (e->y() - last_mouse_position_.y()) * 0.001f;

        if ((e->modifiers() & Qt::ShiftModifier)) {

            // rotation around x
            glm::mat4 model_rotation_matrix_x_axis = glm::rotate(glm::mat4(), glm::radians(dy),
                                                                 camera_.GetHorizontalStrifeMatrix());
            model_rotation_matrix_ *= /*model_rotation_matrix_y_axis **/ model_rotation_matrix_x_axis;

        }

        else {
            camera_.MouseUpdate(glm::vec2(dx, dy));
        }

        emit repaint();
    }

    void BasicViewer::mousePressEvent(QMouseEvent *e) {

        last_mouse_position_ = e->pos();

    }

    void BasicViewer::wheelEvent(QWheelEvent *e) {

        float distance = 0.001f * e->delta();

        camera_.KeyUpdate(Camera::Move::StrafeForward, distance);

        emit repaint();
    }

    void BasicViewer::resizeGL(int width, int height) {

        GL->glViewport(0, 0, width, height);
    }

    void BasicViewer::keyPressEvent(QKeyEvent *e) {

        // Camera movement
        const float distance = 0.3f;

        QEasingCurve curve(QEasingCurve::InOutQuad);

        for (auto t = 0.0f; t < distance; t += 0.02f) {

            float easing_value = static_cast<float>(curve.valueForProgress(t));

            switch (e->key()) {
                case Qt::Key_W:
                    camera_.KeyUpdate(Camera::Move::StrafeForward, easing_value);
                    break;
                case Qt::Key_S:
                    camera_.KeyUpdate(Camera::Move::StrafeBackward, easing_value);
                    break;
                case Qt::Key_A:
                    camera_.KeyUpdate(Camera::Move::StrafeLeft, easing_value);
                    break;
                case Qt::Key_D:
                    camera_.KeyUpdate(Camera::Move::StrafeRight, easing_value);
                    break;
                case Qt::Key_Q:
                    camera_.KeyUpdate(Camera::Move::StrafeUp, easing_value);
                    break;
                case Qt::Key_E:
                    camera_.KeyUpdate(Camera::Move::StrafeDown, easing_value);
                    break;
                case Qt::Key_R:
                    camera_.Reset();
                    break;
                case Qt::Key_O:
                    scale_ *= (1.0f + easing_value);
                    break;
                case Qt::Key_P:
                    scale_ /= (1.0f + easing_value);
                    break;
                default:
                    break;
            }

            emit repaint();
        }
    }

    void BasicViewer::onRenderGrid(int option) {

        render_grid_ = (option == Qt::CheckState::Checked);

        emit repaint();
    }

    void BasicViewer::onRenderPointCloud(int option) {

        render_point_cloud_ = (option == Qt::CheckState::Checked);

        emit repaint();
    }

    void BasicViewer::onRenderTrajectory(int option) {

        render_trajectory_ = (option == Qt::CheckState::Checked);

        emit repaint();
    }

    void BasicViewer::onResetCamera() {

        camera_.Reset();
        emit repaint();
    }

    void BasicViewer::onChangeDensity(int value) {

        makeCurrent();

        density_step_ = value;

        for (auto &frame : keyframes_gl_) {
            frame.SetPointDensityStep(density_step_);
            frame.SetupData();
        }

        for (auto &frame : keyframes_gl_for_inliers_) {
            frame.SetPointDensityStep(density_step_);
            frame.SetupData();
        }

        if (!keyframes_gl_.empty()) {
            auto points_per_frame = keyframes_gl_[0].GetVertexData().size();
            auto total_points = points_per_frame * keyframes_gl_.size();

            emit Message(QString("#Frames : %1, #Points/Frame : %2, #Total Points %3.")
                                 .arg(keyframes_gl_.size())
                                 .arg(points_per_frame)
                                 .arg(total_points));
        }

        if (!keyframes_gl_for_inliers_.empty()) {
            auto points_per_frame = keyframes_gl_for_inliers_[0].GetVertexData().size();
            auto total_points = points_per_frame * keyframes_gl_for_inliers_.size();

            emit Message(QString("#Frames : %1, #Points/Frame : %2, #Total Points %3.")
                                 .arg(keyframes_gl_for_inliers_.size())
                                 .arg(points_per_frame)
                                 .arg(total_points));
        }

        emit repaint();

        doneCurrent();
    }

    void BasicViewer::onTopView(int option) {

        static glm::vec3 old_camera_position;
        static glm::vec3 old_camera_lookat;

        static glm::mat4 last_model_rotation_matrix;
        static glm::mat4 last_model_translation_matrix;


        if (option == Qt::CheckState::Checked) {

            last_model_rotation_matrix = model_rotation_matrix_;
            last_model_translation_matrix = model_translation_matrix_;

            old_camera_position = camera_.GetPosition();
            old_camera_lookat = camera_.GetLookAt();

            camera_.UpdateCameraLookAt(0.0f, -1.0f, -0.001f);
            camera_.UpdateCameraPosition(0.0f, 20.0f, 0.0f);

        }
        else {
            camera_.UpdateCameraPosition(old_camera_position);
            camera_.UpdateCameraLookAt(old_camera_lookat);
            old_camera_position = camera_.GetPosition();
            old_camera_lookat = camera_.GetLookAt();

        }
        emit repaint();
    }

    void BasicViewer::onSpin(int option) {

        if (option == Qt::CheckState::Checked)
            spin_timer_->start(30);
        else
            spin_timer_->stop();
    }

    void BasicViewer::SetKeyFrames(KeyFrames keyframes) {

        makeCurrent();

        emit Message(QString("Viewer received %1 frames.").arg(keyframes.size()));

        keyframes_gl_.clear();

        for (auto i = 0; i < keyframes.size(); ++i) {

            KeyFrameGL keyframe_gl(GL, keyframes[i], density_step_);
            keyframe_gl.SetShaderProgram(shader_program_);
            keyframe_gl.SetupData();
            keyframes_gl_.push_back(keyframe_gl);
        }

        std::cout << "Set up " << keyframes.size() << std::endl;


        trajectory_gl_.clear();
        TrajectoryGL trajectory_gl(GL, keyframes);
        trajectory_gl.SetShaderProgram(shader_program_);
        trajectory_gl.SetupData();
        trajectory_gl_.push_back(trajectory_gl);

        emit repaint();

        if (!keyframes_gl_.empty()) {
            auto points_per_frame = keyframes_gl_[0].GetVertexData().size();
            auto total_points = points_per_frame * keyframes_gl_.size();

            emit Message(QString("#Frames : %1, #Points/Frame : %2, #Total Points %3.")
                                 .arg(keyframes_gl_.size())
                                 .arg(points_per_frame)
                                 .arg(total_points));
        }

        doneCurrent();
    }

    void BasicViewer::SetViewerMode(int mode) {

        mode_ = mode;

        switch (mode) {
            case 0: {
                background_color_ = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
                inliers_pair_gl_.clear();
                keyframes_gl_for_inliers_.clear();
                break;
            }
            case 1:
                background_color_ = glm::vec4(0.09f, 0.0f, 0.2f, 1.0f);
                break;
            default:
                break;
        }

        emit repaint();
    }


    void BasicViewer::SetInliers(CorrespondingPointsPair inliers_pair) {

        makeCurrent();

        assert(!inliers_pair.first.empty() and !inliers_pair.second.empty());

        std::cout << "Inliers of 1 and 2 : " << inliers_pair.first.size() << " : " << inliers_pair.second.size() <<
        std::endl;

        inliers_pair_gl_.clear();
        inliers_pair_gl_.push_back(CorrespondingPointsGL(GL, inliers_pair));
        inliers_pair_gl_[0].SetShaderProgram(shader_program_);
        inliers_pair_gl_[0].SetupData();

        doneCurrent();

        emit repaint();

    }

    void BasicViewer::SetCorrespondingPoints(CorrespondingPointsPair corresponding_points_pair) {

        assert(!corresponding_points_pair.first.empty() and !corresponding_points_pair.second.empty());

        makeCurrent();

        std::cout << "Initial 3D corresponding points : " << corresponding_points_pair.first.size() << " : " <<
        corresponding_points_pair.second.size() << std::endl;

        corresponding_points_pair_gl_.clear();
        corresponding_points_pair_gl_.push_back(CorrespondingPointsGL(GL, corresponding_points_pair));
        corresponding_points_pair_gl_[0].SetShaderProgram(shader_program_);
        corresponding_points_pair_gl_[0].SetupData();

        doneCurrent();

        emit repaint();
    }

    void BasicViewer::SetKeyFramesForInliers(KeyFrames keyframes) {

        makeCurrent();

        emit Message(QString("Viewer received %1 frames.").arg(keyframes.size()));

        if (keyframes.size() == 2) {

            keyframes_gl_for_inliers_.clear();
            keyframes_gl_for_inliers_.push_back(KeyFrameGL(GL, keyframes[0], density_step_));
            keyframes_gl_for_inliers_.push_back(KeyFrameGL(GL, keyframes[1], density_step_));

            keyframes_gl_for_inliers_[0].SetShaderProgram(shader_program_);
            keyframes_gl_for_inliers_[1].SetShaderProgram(shader_program_);

            keyframes_gl_for_inliers_[0].SetupData();
            keyframes_gl_for_inliers_[1].SetupData();
        }

        doneCurrent();

        emit repaint();
    }

    void BasicViewer::onResetModel() {

        model_rotation_matrix_ = glm::mat4();

        scale_ = 1.0f;
        degree_ = 0.0f;
        emit repaint();
    }

    void BasicViewer::onSpin() {

        360.0f == degree_ ? (degree_ = 0.0f) : (degree_ += 0.01f);
        emit repaint();
    }

    QOpenGLShaderProgram *BasicViewer::SetupShaderProgram(const QString &vertex_shader_source_path,
                                                          const QString &fragment_shader_source_path,
                                                          QObject *parent) {

        QResource vertex_shader_resource(vertex_shader_source_path);
        QResource fragment_shader_resource(fragment_shader_source_path);

        auto vertex_shader_code = NiS::ConvertConstCStrToStdString(vertex_shader_resource.data(),
                                                                   vertex_shader_resource.size());
        auto fragment_shader_code = NiS::ConvertConstCStrToStdString(fragment_shader_resource.data(),
                                                                     fragment_shader_resource.size());

        QOpenGLShader vertex_shader(QOpenGLShader::Vertex, 0);
        QOpenGLShader fragment_shader(QOpenGLShader::Fragment, 0);

        vertex_shader.compileSourceCode(vertex_shader_code.c_str());
        fragment_shader.compileSourceCode(fragment_shader_code.c_str());

        assert(vertex_shader.isCompiled() and fragment_shader.isCompiled());

        QOpenGLShaderProgram *program = new QOpenGLShaderProgram(parent);

        program->addShader(&vertex_shader);
        program->addShader(&fragment_shader);

        program->link();

        assert(program->isLinked());

        return program;
    }

    void BasicViewer::SetPointPair(const PointPair &point_pair) {

        makeCurrent();

        point_pair_gl_.clear();

        std::cout << point_pair.first;
        std::cout << point_pair.second;

        auto point_pair_gl = PointPairGL(GL, point_pair);
        point_pair_gl.SetShaderProgram(shader_program_);
        point_pair_gl.SetupData();
        point_pair_gl_.push_back(point_pair_gl);

        doneCurrent();
    }

}
