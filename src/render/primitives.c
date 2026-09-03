#include "render/primitives.h"

#include <GLFW/glfw3.h>

void primitives_draw_cube(void) {
    glBegin(GL_QUADS);
    glNormal3f(0,0,1);
    glVertex3f(-1,-1,1); glVertex3f(1,-1,1); glVertex3f(1,1,1); glVertex3f(-1,1,1);
    glNormal3f(0,0,-1);
    glVertex3f(1,-1,-1); glVertex3f(-1,-1,-1); glVertex3f(-1,1,-1); glVertex3f(1,1,-1);
    glNormal3f(-1,0,0);
    glVertex3f(-1,-1,-1); glVertex3f(-1,-1,1); glVertex3f(-1,1,1); glVertex3f(-1,1,-1);
    glNormal3f(1,0,0);
    glVertex3f(1,-1,1); glVertex3f(1,-1,-1); glVertex3f(1,1,-1); glVertex3f(1,1,1);
    glNormal3f(0,1,0);
    glVertex3f(-1,1,1); glVertex3f(1,1,1); glVertex3f(1,1,-1); glVertex3f(-1,1,-1);
    glNormal3f(0,-1,0);
    glVertex3f(-1,-1,-1); glVertex3f(1,-1,-1); glVertex3f(1,-1,1); glVertex3f(-1,-1,1);
    glEnd();
}

void primitives_draw_box(float width, float height, float depth) {
    glPushMatrix();
    glScalef(width * .5f, height * .5f, depth * .5f);
    primitives_draw_cube();
    glPopMatrix();
}
