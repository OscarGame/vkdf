#include "vkdf-object.hpp"
#include "vkdf-util.hpp"

inline static void
init_object(VkdfObject *obj, const glm::vec3 &pos)
{
   obj->pos = pos;
   obj->rot = glm::vec3(0.0f, 0.0f, 0.0f);
   obj->scale = glm::vec3(1.0f, 1.0f, 1.0f);

   memset(obj->priv_data.i32, -1, sizeof(obj->priv_data));

   obj->dirty = true;
   obj->dirty_model_matrix = true;
   obj->dirty_box = true;
   obj->dirty_mesh_boxes = true;
}

VkdfObject *
vkdf_object_new_from_mesh(const glm::vec3 &pos, VkdfMesh *mesh)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = vkdf_model_new();
   vkdf_model_add_mesh(obj->model, mesh);
   vkdf_model_compute_box(obj->model);

   return obj;
}

VkdfObject *
vkdf_object_new_from_model(const glm::vec3 &pos,
                           VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

VkdfObject *
vkdf_object_new(const glm::vec3 &pos, VkdfModel *model)
{
   VkdfObject *obj = g_new0(VkdfObject, 1);

   init_object(obj, pos);

   obj->model = model;

   return obj;
}

void
vkdf_object_free(VkdfObject *obj)
{
   // Models are not owned by the objects
   if (obj->mesh_boxes)
      g_free(obj->mesh_boxes);
   g_free(obj);
}

glm::mat4
vkdf_object_get_model_matrix(VkdfObject *obj)
{
   if (!obj->dirty_model_matrix)
      return obj->model_matrix;

   obj->model_matrix =
      vkdf_compute_model_matrix(obj->pos, obj->rot, obj->scale, obj->rot_origin);

   vkdf_object_set_dirty_model_matrix(obj, false);
   return obj->model_matrix;
}

static glm::mat4
get_rotation_matrix_at_position(glm::vec3 pos, glm::vec3 rot)
{
   glm::mat4 Model(1.0f);
   Model = glm::translate(Model, pos);
   // FIXME: use quaternion
   if (rot.y)
      Model = glm::rotate(Model, DEG_TO_RAD(rot.y), glm::vec3(0, 1, 0));
   if (rot.x)
      Model = glm::rotate(Model, DEG_TO_RAD(rot.x), glm::vec3(1, 0, 0));
   if (rot.z)
      Model = glm::rotate(Model, DEG_TO_RAD(rot.z), glm::vec3(0, 0, 1));
   Model = glm::translate(Model, -pos);

   return Model;
}

static void
compute_box(VkdfObject *obj)
{
   assert(obj->model);

   /* Compute the size of the box based on the object's scale */
   obj->box.w = vkdf_object_width(obj) / 2.0f;
   obj->box.d = vkdf_object_depth(obj) / 2.0f;
   obj->box.h = vkdf_object_height(obj) / 2.0f;

   /* Put the box at the object's position. If the model is not centered
    * at the origin we need to account for that too.
    */
   obj->box.center = obj->pos + obj->model->box.center * obj->scale;

   /* If the object is rotated we need to rotate the box around the object's
    * position (not the object's position offset by the model's position).
    */
   if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
      glm::mat4 rot_matrix =
         get_rotation_matrix_at_position(obj->pos + obj->rot_origin, obj->rot);
      vkdf_box_transform(&obj->box, &rot_matrix);
   }

   vkdf_object_set_dirty_box(obj, false);
}

VkdfBox *
vkdf_object_get_box(VkdfObject *obj)
{
   if (obj->dirty_box)
      compute_box(obj);
   return &obj->box;
}

static void
compute_mesh_boxes(VkdfObject *obj)
{
   assert(obj->model);
   const VkdfModel *model = obj->model;

   uint32_t num_meshes = model->meshes.size();
   assert(num_meshes > 0);

   if (!obj->mesh_boxes)
      obj->mesh_boxes = g_new(VkdfBox, num_meshes);

   for (uint32_t i = 0; i < num_meshes; i++) {
      // Get the mesh's box, scaled by the object dimensions
      VkdfBox *box = &obj->mesh_boxes[i];
      vkdf_mesh_get_scaled_box(model->meshes[i], obj->scale, box);

      // Apply the object translation transform to the box
      box->center += obj->pos;

      // Apply the object rotation transform to the box
      if (obj->rot.x != 0.0f || obj->rot.y != 0.0f || obj->rot.z != 0.0f) {
         glm::mat4 Model(1.0f);
         Model = glm::translate(Model, box->center);
         // FIXME: use quaternion
         if (obj->rot.y)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.y), glm::vec3(0, 1, 0));
         if (obj->rot.x)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.x), glm::vec3(1, 0, 0));
         if (obj->rot.z)
            Model = glm::rotate(Model, DEG_TO_RAD(obj->rot.z), glm::vec3(0, 0, 1));
         Model = glm::translate(Model, -box->center);
         vkdf_box_transform(box, &Model);
      }
   }

   vkdf_object_set_dirty_mesh_boxes(obj, false);
}

const VkdfBox *
vkdf_object_get_mesh_boxes(VkdfObject *obj)
{  if (obj->dirty_mesh_boxes)
      compute_mesh_boxes(obj);
   return obj->mesh_boxes;
}

bool
vkdf_object_get_visible_meshes(VkdfObject *obj,
                               const VkdfBox *frustum_box,
                               const VkdfPlane *frustum_planes,
                               bool *visible)
{
   bool any_visible = false;

   const VkdfBox *mesh_boxes = vkdf_object_get_mesh_boxes(obj);
   const VkdfModel *model = obj->model;
   for (uint32_t i = 0; i < model->meshes.size(); i++) {
      VkdfBox *box = (VkdfBox *) &mesh_boxes[i];
      visible[i] =
         vkdf_box_is_in_frustum(box, frustum_box, frustum_planes) != OUTSIDE;
      any_visible |= visible[i];
   }

   return any_visible;
}
