#pragma GCC optimize(3,"Ofast","inline")
#include <iostream>
#include <cmath>
#include <vector>
#include <omp.h>
#include <thread>
#include <mutex>
#include <windows.h>
#include "darknet.h"
const short AI = 1; // AI������
const short OP = -1; // ���ֵ�����
const short BLK = 0; // �հ�

std::mutex mtx;
static omp_lock_t lock;

struct TrainData
{
    float input[4][81] = { 0 };
    float output[81] = { 0 };
};
std::vector<TrainData> trainData;

class AlphaPig
{
public:
    short board[81] = { 0 };
    bool air_vis[81];
    int mcts_cnt = 0, pl_cnt = 0;
    network* net;

    // ���ؿ������ڵ�
    struct treeNode
    {
        // ���̼���ɫ
        short board[81] = { 0 };
        short color;

        // ��һ������λ��
        short available_me[81], available_his[81];
        int available_me_size = 0, available_his_size = 0;
        bool available_me_map[81] = { false }, available_his_map[81] = { false };

        // �ڵ���Ӯͳ��
        double value = 0;
        int total = 0;
        int win = 0;
        bool fail = false;

        // ���ڵ�
        treeNode* father = NULL;

        // ���ӽڵ�
        treeNode* children[81];
        int children_size = 0;

        // ����̽������
        short policy[81];
        int policy_size = 0;

        // ��֧�����������
        int complete = 0;

        // ���һ��λ��
        short last_p = -1;

        // ���
        int depth = 0;
    };

    treeNode* root = NULL;

    // ����Ȩ��
    struct Weight
    {
        short p;
        float w;
    };

    // Ȩ������
    static bool weightCMP(const Weight& a, const  Weight& b)
    {
        return a.w > b.w;
    }

    // �ƶ�λ��
    inline short moveTo(short p, short dir)
    {
        switch (dir)
        {
        case 0:
            return (p += 9) < 81 ? p : -1;
        case 1:
            return (p -= 9) >= 0 ? p : -1;
        case 2:
            return p % 9 < 8 ? p + 1 : -1;
        case 3:
            return p % 9 > 0 ? p - 1 : -1;
        }
        return p;
    }

    // �ж��Ƿ�����
    bool hasAir(short mBoard[], short p)
    {
        air_vis[p] = true;
        bool flag = false;
        for (short dir = 0; dir < 4; dir++)
        {
            short dp = moveTo(p, dir);
            if (dp >= 0)
            {
                if (mBoard[dp] == BLK)
                    flag = true;
                if (mBoard[dp] == mBoard[p] && !air_vis[dp])
                    if (hasAir(mBoard, dp))
                        flag = true;
            }
        }
        return flag;
    }

    // �ж��Ƿ��������
    bool judgeAvailable(short mBoard[], short p, short col)
    {
        if (mBoard[p]) return false;
        mBoard[p] = col;
        memset(air_vis, 0, sizeof(air_vis));
        if (!hasAir(mBoard, p))
        {
            mBoard[p] = 0;
            return false;
        }
        for (short dir = 0; dir < 4; dir++)
        {
            short dp = moveTo(p, dir);
            if (dp >= 0)
            {
                if (mBoard[dp] && !air_vis[dp])
                    if (!hasAir(mBoard, dp))
                    {
                        mBoard[p] = 0;
                        return false;
                    }
            }
        }
        mBoard[p] = 0;
        return true;
    }

    // ɨ��������ӵ�λ��
    void scanAvailable(treeNode* node)
    {
        short* board = node->board;
        bool ban_his[81] = { false }, ban_me[81] = { false }; // ����
        bool vis[81] = { false };

        for (short dir = 0; dir < 4; dir++)
        {
            short p = moveTo(node->last_p, dir);
            if(p == -1) continue;
            if (board[p] == BLK)
            {
                ban_me[p] = !judgeAvailable(board, p, node->color);
                ban_his[p] = !judgeAvailable(board, p, -node->color);
            }
            else if (!vis[p])
            {
                short queue[81], q_left = 0, q_right = 0;
                bool tgas_vis[81] = { false };
                short tgas = 0;
                int tgas_size = 0;
                queue[q_right++] = p;
                while (q_left != q_right)
                {
                    short pq = queue[q_left++];
                    q_left %= 81;
                    vis[pq] = true;
                    for (short dir = 0; dir < 4; dir++)
                    {
                        short dp = moveTo(pq, dir);
                        if (dp >= 0)
                        {
                            if (board[dp] == BLK && !tgas_vis[dp])
                            {
                                tgas_vis[dp] = true;
                                tgas_size++;
                                tgas = dp;
                            }
                            else if (board[dp] == board[pq] && !vis[dp])
                            {
                                queue[q_right++] = dp;
                                q_right %= 81;
                            }
                        }
                    }
                }
                if (tgas_size == 1)
                {
                    ban_me[tgas] = !judgeAvailable(board, tgas, node->color);
                    ban_his[tgas] = !judgeAvailable(board, tgas, -node->color);
                }
            }
        }

        for (int i = 0; i < node->father->available_me_size; i++)
        {
            short p = node->father->available_me[i];
            if (board[p] == BLK && !ban_his[p])
            {
                bool flag = true;
                for (short dir = 0; dir < 4; dir++)
                {
                    short dp = moveTo(p, dir);
                    if (dp >= 0 && board[dp] != node->color)
                    {
                        node->available_his[(node->available_his_size)++] = p;
                        node->available_his_map[p] = true;
                        break;
                    }
                }
            }
        }

        for (int i = 0; i < node->father->available_his_size; i++)
        {
            short p = node->father->available_his[i];
            if (board[p] == BLK && !ban_me[p])
            {
                for (short dir = 0; dir < 4; dir++)
                {
                    short dp = moveTo(p, dir);
                    if (dp >= 0 && board[dp] != -node->color)
                    {
                        node->available_me[(node->available_me_size)++] = p;
                        node->available_me_map[p] = true;
                        break;
                    }
                }
            }
        }

    }

    // ���Ժ���
    void makePolicy(treeNode* node)
    {
        // ���Ȳ�����
        short eye[81] = { 0 }, no_eye[81] = { 0 };
        int eye_size = 0, no_eye_size = 0;
        short col = -node->color;

        for (int i = 0; i < node->available_his_size; i++)
        {
            short p = node->available_his[i];
            bool is_eye = true;
            for (short dir = 0; dir < 4; dir++)
            {
                short dp = moveTo(p, dir);
                if (dp >= 0 && node->board[dp] != col)
                {
                    is_eye = false;
                    break;
                }
            }
            if (is_eye)
            {
                eye[eye_size++] = p;
            }
            else
            {
                no_eye[no_eye_size++] = p;
            }
        }

        // ֻʣ���ۣ�ֱ�ӷ���
        if (no_eye_size == 0)
        {
            memcpy(node->policy, eye, sizeof(node->policy));
            node->policy_size = eye_size;

        }
        else
        {
            memcpy(node->policy, no_eye, sizeof(node->policy));
            node->policy_size = no_eye_size;
        }

        // ����
        for (int i = node->policy_size - 1; i >= 0; i--)
            std::swap(node->policy[i], node->policy[rand() % (i + 1)]);
    }

    // ��ֵ����
    inline double calcValue(treeNode* node)
    {
        // ��ʱ�ÿ�������λ�ù�ֵ
        double a = node->available_me_size;
        double b = node->available_his_size;
        if (a == 0 && b == 0 && node->father != NULL)
        {
            return -calcValue(node->father);
        }
        return 1 / (1 + pow(2.7182818284590452354, b - a)) * 2 - 1;
    }

    // �½��ڵ�
    inline treeNode* newNode(treeNode* father, short p)
    {
        treeNode* newNode = new treeNode();
        memcpy(newNode->board, father->board, sizeof(board));
        newNode->color = -father->color;
        newNode->last_p = p;
        newNode->board[p] = newNode->color;
        newNode->father = father;
        newNode->depth = father->depth + 1;
        scanAvailable(newNode);
        makePolicy(newNode);
        father->children[father->children_size++] = newNode;
        return newNode;
    }

    // ɾ����֧
    void deleteTree(treeNode* node)
    {
        if (node != NULL)
        {
            while (node->children_size > 0)
                deleteTree(node->children[--node->children_size]);
            delete node;
        }
    }

    // �ڵ��������
    inline bool finishNode(treeNode* node)
    {
        return (node->available_his_size > 0 && node->policy_size == 0 && node->complete == node->children_size) || (node->available_his_size == 0 && node->complete > 0);
    }

    // ѡ�������ӽڵ�
    treeNode* bestChild(treeNode* node)
    {
        treeNode* max_node = NULL;
        bool Allcomplete = true;
        double max = -1e10;
        for (int i = 0; i < node->children_size; i++)
        {
            treeNode* t_node = node->children[i];
            if (finishNode(t_node))
                continue;

            // �������������㷨
            double probability = t_node->value / t_node->total + 1.4142135623731 * sqrt(log(t_node->father->total) / t_node->total);
            if (probability > max)
            {
                max = probability;
                max_node = t_node;
                Allcomplete = false;
            }
        }
        return Allcomplete ? NULL : max_node;
    }

    // ѡ��&ģ��&����
    bool select(treeNode* node)
    {
        // ѡ��
        while (node->available_his_size > 0) // ����ڵ����Ϸû�н���
        {
            if (node->policy_size > 0) // ����ڵ��п��ж�����δ����չ��
            {
                // ��չ
                node = newNode(node, node->policy[--node->policy_size]);
                break;
            }
            else   // ����ڵ����п��ж���������չ��
            {
                node = bestChild(node);
                if (node == NULL)
                    return false;
            }
        }
        double value;

        // ģ��
        if (node->available_his_size == 0)   // �Ƿ����
        {
            node->complete = 1;
            treeNode* father = node->father;
            father->complete++;
            father->fail = true;
            while (father != NULL)
            {
                if (father->father == NULL)
                    break;
                if (finishNode(father))
                {
                    father->father->complete++;
                    if (father->fail == true)
                        father->father->win++;
                    if (father->win == father->complete)
                        father->father->fail = true;
                }
                father = father->father;
            }
            value = 1;
        }
        else
        {
            value = calcValue(node);
        }

        // ����
        while (node != NULL)
        {
            node->total += 1;
            node->value += value;
            node = node->father;
            value = -value;
        }

        return true;
    }

    // ��ʼ����
    void initRoot(short last_p)
    {
        root = new treeNode();
        memcpy(root, board, sizeof(board));
        root->color = OP;
        root->last_p = last_p;
        for (int i = 0; i < 81; i++)
        {
            if (judgeAvailable(root->board, i, root->color))
                root->available_me[(root->available_me_size)++] = i;
            if (judgeAvailable(root->board, i, -root->color))
                root->available_his[(root->available_his_size)++] = i;
        }
        makePolicy(root);
    }

    int choose(int last_p)
    {
        initRoot(last_p);

        // ������
        if (root->available_his_size == 0)
        {
            deleteTree(root);
            return -1;
        }

        // MCTSģ��
        for (int i = 0; i < 2000000 && select(root); i++);

        // ������
        if (finishNode(root) && root->win == root->complete)
        {
            deleteTree(root);
            return -1;
        }

        // ѡ����õ��·�
        treeNode* max_node = root->children[0];
        double max = -1e10;
        for (int i = 0; i < root->children_size; i++)
        {
            treeNode* t_node = root->children[i];
            double probability = t_node->value / t_node->total;
            if (probability > max)
            {
                max = probability;
                max_node = t_node;
            }
        }

        int ai = max_node->last_p;

        TrainData tdata;

        // ��������
        for (int i = 0; i < 81; i++)
        {
            if(board[i]==AI)
                tdata.input[0][i] = 1; // ��һͨ��Ϊ�ҷ�����
            if(board[i]==OP)
                tdata.input[1][i] = 1; // �ڶ�ͨ��Ϊ�Է�����
            if(board[i]==BLK)
                tdata.input[2][i] = 1; // ����ͨ��Ϊ��λ
        }
        if(last_p>=0)
            tdata.input[3][last_p] = 1; // ����ͨ��Ϊ���һ��λ��

        // �������λ��
        tdata.output[ai] = 1;

        // ����ѵ����
        omp_set_lock(&lock);
        trainData.push_back(tdata);
        omp_unset_lock(&lock);

        deleteTree(root);

        return ai;
    }
};

void playGame(int i)
{
    while(true)
    {
        srand((unsigned)clock() + i);
        AlphaPig alphaPig1;
        AlphaPig alphaPig2;
        int a1 = -1, a2 = -1;
        while (true)
        {
            a1 = alphaPig1.choose(a2);
            if (a1 == -1)
            {
                break;
            }
            alphaPig1.board[a1] = AI;
            alphaPig2.board[a1] = OP;

            a2 = alphaPig2.choose(a1);
            if (a2 == -1)
            {
                break;
            }
            alphaPig1.board[a2] = OP;
            alphaPig2.board[a2] = AI;
        }
    }
}

int main()
{
    srand((unsigned)time(NULL));
    omp_init_lock(&lock);

    // ѵ������
    float avg_loss = -1;
    char* cfgfile = (char*)"policy_network.cfg";
    char* weightfile = (char*)"policy_network.weights";

    network* net = load_network(cfgfile, weightfile, false);
    if (net == NULL)
        return 0;

    size_t n = net->batch * net->subdivisions;
    printf("Learning Rate: %g, Momentum: %g, Decay: %g\n", net->learning_rate, net->momentum, net->decay);

    // ��¼��ʼʱ��
    double time = what_time_is_it_now();

    // ��ʼ���������߳�
    for(int i = 0; i < 32; i++)
    {
        std::thread t(playGame, i);
        t.detach();
    }

    // �ȴ�����
    while(true)
    {
        int length;
        omp_set_lock(&lock);
        length = trainData.size();
        omp_unset_lock(&lock);
        if(length > 30000)
            break;
        Sleep(1000);
    }

    // ��ʼѵ��
    while (get_current_batch(net) < net->max_batches || net->max_batches == 0)
    {
        omp_set_lock(&lock);
        size_t trainDataSize = trainData.size();
        data d = { 0 };
        d.X = make_matrix(n, 4 * 81);
        d.y = make_matrix(n, 81);
        for (size_t i = 0; i < n; i++)
        {
            TrainData tdata = trainData[rand() % trainDataSize];
            float* input = d.X.vals[i];
            float* output = d.y.vals[i];
            for (int j = 0; j < 81; j++)
            {
                input[0 * 81 + j] = tdata.input[0][j];
                input[1 * 81 + j] = tdata.input[1][j];
                input[2 * 81 + j] = tdata.input[2][j];
                input[3 * 81 + j] = tdata.input[3][j];
                output[j] = tdata.output[j];
            }

            int flip = rand() % 2;
            int rotate = rand() % 4;
            image in = float_to_image(9, 9, 4, input);
            image out = float_to_image(9, 9, 1, output);
            if (flip)
            {
                flip_image(in);
                flip_image(out);
            }
            rotate_image_cw(in, rotate);
            rotate_image_cw(out, rotate);
        }
        omp_unset_lock(&lock);

        float loss = train_network(net, d);
        free_data(d);

        if (avg_loss == -1) avg_loss = loss;
        avg_loss = avg_loss * .99 + loss * .01;

        if (get_current_batch(net) % 10 == 0)
        {
            printf("%d : %f, %f avg, %f rate, %lf seconds, %ld images, %ld total\n", (int)get_current_batch(net), loss, avg_loss, get_current_rate(net), what_time_is_it_now() - time, (long)*net->seen, trainDataSize);
        }

        if (get_current_batch(net) % 1000 == 0)
        {
            save_weights(net, weightfile);
        }
    }

    free_network(net);
    return 0;
}
