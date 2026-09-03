#ifndef CORE_DIALOGUE_H
#define CORE_DIALOGUE_H

#define DIALOGUE_MAX_NODES 64
#define DIALOGUE_MAX_CHOICES 4
#define DIALOGUE_SPEAKER_LENGTH 48
#define DIALOGUE_TEXT_LENGTH 384
#define DIALOGUE_CHOICE_LENGTH 128
#define DIALOGUE_END (-1)

typedef struct {
    char text[DIALOGUE_CHOICE_LENGTH];
    int next_node;
} DialogueChoice;

typedef struct {
    char speaker[DIALOGUE_SPEAKER_LENGTH];
    char text[DIALOGUE_TEXT_LENGTH];
    DialogueChoice choices[DIALOGUE_MAX_CHOICES];
    int choice_count;
} DialogueNode;

typedef struct {
    char id[DIALOGUE_SPEAKER_LENGTH];
    DialogueNode nodes[DIALOGUE_MAX_NODES];
    int node_count;
    int start_node;
} DialogueTree;

typedef struct {
    const DialogueTree* tree;
    int current_node;
    int selected_choice;
    int active;
} DialogueState;

void dialogue_init(DialogueState* dialogue);
int dialogue_tree_load(DialogueTree* tree, const char* path,
                       char* error, int error_size);
int dialogue_begin(DialogueState* dialogue, const DialogueTree* tree);
void dialogue_close(DialogueState* dialogue);
int dialogue_move_selection(DialogueState* dialogue, int direction);
int dialogue_choose(DialogueState* dialogue);
const DialogueNode* dialogue_current(const DialogueState* dialogue);
int dialogue_handle_keys(DialogueState* dialogue, int up, int down,
                         int accept, int cancel);

#endif
