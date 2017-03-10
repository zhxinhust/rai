#include "percept.h"
#include <Gui/opengl.h>

double alpha = .2;


template<> const char* mlr::Enum<Percept::Type>::names []=
  { "PT_cluster", "PT_plane", "PT_box", "PT_mesh", "PT_alvar", "PT_optitrackmarker", "PT_optitrackbody", NULL };


Percept::Percept(Type type)
  : type(type), transform(0), frame(0){
}

Percept::Percept(Type type, const mlr::Transformation& t)
  : type(type), transform(t), frame(0){
}

double Percept::idMatchingCost(const Percept& other){
  mlr::Vector diff = (this->transform.pos - other.transform.pos);
  return diff.length();
}

double Percept::fuse(Percept* other){
  transform.pos = (1.-alpha)*transform.pos + alpha*other->transform.pos;
  transform.rot.setInterpolate(alpha, transform.rot, other->transform.rot);
  return 0.;
}

void Percept::write(ostream& os) const{
  os <<type <<'_' <<id <<" <" <<transform <<">:";
//  os <<" trans=" <<transform <<" frame=" <<frame <<" type=" <<type;
}

//============================================================================

PercCluster::PercCluster(arr mean, arr points, std::string _frame_id)
  : Percept(Type::PT_cluster),
    mean(mean),
    points(points) {
  frame_id = _frame_id;
}


PercCluster::PercCluster(const PercCluster& obj)
  : Percept(obj){
  this->frame_id = obj.frame_id;
  this->mean = obj.mean;
  this->points = obj.points;
  this->type = obj.type;
  this->relevance = obj.relevance;
  this->id = obj.id;
  this->transform = obj.transform;
  this->frame = obj.frame;
}

double PercCluster::idMatchingCost(const Percept& other){
  if(other.type!=PT_cluster) return -1.;
  mlr::Vector diff = (this->frame * mlr::Vector(this->mean)) -
                     (dynamic_cast<const PercCluster*>(&other)->frame * mlr::Vector(dynamic_cast<const PercCluster*>(&other)->mean));
  return diff.length();
}

void PercCluster::write(ostream& os) const{
  os <<"cluster_" <<id <<": mean=" <<mean;
  Percept::write(os);
}

//============================================================================

double PercMesh::fuse(Percept* other){
  Percept::fuse(other);
  const PercMesh *x = dynamic_cast<const PercMesh*>(other);
  CHECK(x,"can't fuse " <<type <<" with "<<other->type);
  mesh = x->mesh;
  return 0.;
}

//============================================================================

PercPlane::PercPlane()
  : Percept(Type::PT_plane) {}

PercPlane::PercPlane(const mlr::Transformation& t, const mlr::Mesh& hull)
  : Percept(Type::PT_plane, t), hull(hull) {}

double PercPlane::idMatchingCost(const Percept& other){
  if(other.type!=PT_plane) return -1.;
  const PercPlane* otherPlane = dynamic_cast<const PercPlane*>(&other);
  if(!otherPlane){ MLR_MSG("WHY?????"); return -1.; }
  CHECK(otherPlane,"");
  mlr::Vector diff = (this->transform.pos - otherPlane->transform.pos);
  return diff.length();
}

double PercPlane::fuse(Percept* other){
  Percept::fuse(other);
  const PercPlane *x = dynamic_cast<const PercPlane*>(other);
  CHECK(x,"can't fuse " <<type <<" with "<<other->type);
  hull = x->hull;
  return 0.;
}

void PercPlane::write(ostream& os) const{
  Percept::write(os);
//  os <<"plane_" <<id <<":"; // center=" <<center <<" normal=" <<normal;
}

void PercPlane::syncWith(mlr::KinematicWorld &K){
  mlr::String plane_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(plane_name, false);
  if (not body) {
    //cout << plane_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = plane_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = plane_name;
    shape->type = mlr::ST_pointCloud;
//    shape = new mlr::Shape(K, *body);
//    shape->name = plane_name;
//    shape->type = mlr::ST_marker;
//    shape->size[0] = shape->size[1] = shape->size[2] = shape->size[3] = .2;
//    stored_planes.append(id);
  }
  body->X = transform;

  body->shapes(0)->mesh = hull;

  for(mlr::Shape *s:body->shapes) s->X = body->X * s->rel;
}

void PercPlane::glDraw(OpenGL& gl){
  hull.glDraw(gl);

//  if(hull.C.N==3){
//    glColor(hull.C(0), hull.C(1), hull.C(2), 1.f);
//  }

//  mlr::Transformation t;
//  t.pos.set(center);
//  t.rot.setDiff(Vector_x, normal);
//  glPushMatrix();
//  glTransform(t);
  glRotatef(-90.f, 0.f, 1.f, 0.f);
  glLineWidth(3.f);
  glScalef(.3, .3, .3);
  glDrawAxis();
//  glPopMatrix();
  glLineWidth(1.f);
}

//============================================================================

//PercBox::PercBox(const PercBox& box)
//  : Percept(box), size(box.size) {}

PercBox::PercBox(const mlr::Transformation& t, const arr& size, const arr& color)
  : Percept(Type::PT_box, t), size(size), color(color){
}

double PercBox::fuse(Percept* other){
  //check flip by 180
  mlr::Quaternion qdiff;
  qdiff = (-transform.rot) * other->transform.rot;
  double score_0 = qdiff.sqrDiffZero();
  qdiff.addZ(+MLR_PI);  double score_1 = qdiff.sqrDiffZero(); //flip by 180
  if(score_1<score_0) other->transform.rot.addZ(-MLR_PI);

  if(size(0)>.8*size(1) && size(0)<1.2*size(1)){ //almost quadratic shape -> check flip by 90
    qdiff = (-transform.rot) * other->transform.rot;
    double score_0 = qdiff.sqrDiffZero();
    qdiff.addZ(-0.5*MLR_PI);  double score_1 = qdiff.sqrDiffZero();
    qdiff.addZ(    +MLR_PI);  double score_2 = qdiff.sqrDiffZero();
    ////  LOG(0) <<"base=" <<transform.rot <<" in=" <<other->transform.rot;
    if(score_1<score_0 && score_1<score_2) other->transform.rot.addZ(-0.5*MLR_PI);
    if(score_2<score_0 && score_2<score_1) other->transform.rot.addZ(+0.5*MLR_PI);
  }

  Percept::fuse(other);
  const PercBox *x = dynamic_cast<const PercBox*>(other);
  CHECK(x,"can't fuse " <<type <<" with "<<other->type);
  size = (1.-alpha)*size + alpha*x->size;
  color = (1.-alpha)*color + alpha*x->color;
  return 0.;
}

void PercBox::syncWith(mlr::KinematicWorld &K){
  mlr::String box_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(box_name, false);
  if (not body) {
    //cout << plane_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = box_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = box_name;
    shape->type = mlr::ST_box;
  }
  body->X = transform;
  for(mlr::Shape *s:body->shapes) s->X = body->X * s->rel;

  mlr::Shape *shape = body->shapes(0);
  shape->size[0] = size(0);
  shape->size[1] = size(1);
  shape->size[2] = size(2);
  shape->size[3] = 0.;
  shape->color[0] = color(0);
  shape->color[1] = color(1);
  shape->color[2] = color(2);
  shape->color[3] = .8;
  shape->mesh.C = color;
}

void PercBox::glDraw(OpenGL&){
  CHECK_EQ(size.N, 3, "");
  glDrawAxes(.2);
  glLineWidth(3);
  glDrawBox(size.elem(0), size.elem(1), size.elem(2), true);
}

//============================================================================

PercAlvar::PercAlvar(std::string _frame_id)
  : Percept(Type::PT_alvar){
  frame_id = _frame_id;
}

PercAlvar::PercAlvar(const PercAlvar& obj)
  : Percept(obj){
  this->frame_id = obj.frame_id;
  this->type = obj.type;
  this->relevance = obj.relevance;
  this->id = obj.id;
  this->transform = obj.transform;
  this->frame = obj.frame;
}

double PercAlvar::idMatchingCost(const Percept& other){
  if(other.type!=PT_alvar) return -1.;
  mlr::Vector dist = (this->frame * this->transform.pos) - (dynamic_cast<const PercAlvar*>(&other)->frame * dynamic_cast<const PercAlvar*>(&other)->transform.pos);
  return dist.length();
}

void PercAlvar::write(ostream& os) const{
  os <<"alvar_" <<id <<":";
  Percept::write(os);
}

//============================================================================

void PercCluster::syncWith(mlr::KinematicWorld& K){
  mlr::String cluster_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(cluster_name, false);
  if (not body) {
    //cout << cluster_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = cluster_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = cluster_name;
    shape->type = mlr::ST_pointCloud;
    shape = new mlr::Shape(K, *body);
    shape->name = cluster_name;
    shape->type = mlr::ST_marker;
    shape->size[0] = shape->size[1] = shape->size[2] = shape->size[3] = .2;
//    stored_clusters.append(id);
  }
  body->X = frame;
  //frame = body->X;
  body->shapes(0)->mesh.V = points;

  mlr::Vector cen = body->shapes(0)->mesh.center();
  body->X.addRelativeTranslation(cen);
  body->shapes(0)->rel.rot = body->X.rot;
  body->X.rot.setZero();

  transform = body->X;
  //((Cluster*)cluster)->mean = ARR(cen.x, cen.y, cen.z);
  /* If we change the mean, we compare the transformed mean to an untransformed mean later...*/
}

//============================================================================

void PercAlvar::syncWith(mlr::KinematicWorld& K){
  mlr::String alvar_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(alvar_name, false);
  if (not body) {
//    cout << alvar_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = alvar_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = alvar_name;
    shape->type = mlr::ST_marker;
    shape->size[0] = shape->size[1] = shape->size[2] = shape->size[3] = .2;
//    stored_alvars.append(id);
  }

  body->X = frame * transform;
  body->shapes.first()->X = body->X;
}

void OptitrackBody::syncWith(mlr::KinematicWorld &K){
  mlr::String optitrackbody_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(optitrackbody_name, false);
  if (not body) {
    cout << optitrackbody_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = optitrackbody_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = optitrackbody_name;
    shape->type = mlr::ST_marker;
    shape->size[0] = shape->size[1] = shape->size[2] = shape->size[3] = .1;
//    stored_optitrackbodies.append(id);
  }

  body->X = frame * transform;
  body->shapes.first()->X = body->X;
}

void OptitrackMarker::syncWith(mlr::KinematicWorld &K){
  mlr::String optitrackmarker_name = STRING("perc_" << id);

  mlr::Body *body = K.getBodyByName(optitrackmarker_name, false);
  if (not body) {
    cout << optitrackmarker_name << " does not exist yet; adding it..." << endl;
    body = new mlr::Body(K);
    body->name = optitrackmarker_name;
    mlr::Shape *shape = new mlr::Shape(K, *body);
    shape->name = optitrackmarker_name;
    shape->type = mlr::ST_sphere;
    shape->size[0] = shape->size[1] = shape->size[2] = shape->size[3] = .03;
//    stored_optitrackmarkers.append(id);
  }

  body->X = frame * transform;

  //((Alvar*)alvar)->transform = body->X;
}

