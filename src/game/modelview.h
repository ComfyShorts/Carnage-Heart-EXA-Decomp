/* A debug model viewer (--gmo FILE, or --oke ID for an assembled OKE body): draws
 * one model turning in front of a camera instead of the title, to check the model
 * reader and the 3D renderer. */
#ifndef GAME_MODELVIEW_H
#define GAME_MODELVIEW_H

extern const char *g_modelview_path;     /* model to show, or NULL */
extern const char *g_modelview_textures; /* optional: a model whose images it uses */
extern float g_modelview_angle;          /* starting turn, degrees */
extern int g_modelview_oke;              /* OKE body id to show instead, or -1 */
extern int g_modelview_pose;             /* its pose */

void modelview_start(void);
void modelview_update(float dt);

#endif
