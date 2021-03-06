#ifndef STATIC_TREE_H
#define STATIC_TREE_H

#include <math.h>
#include <ostream>
#include <fstream>
#include <iostream>
#include <time.h>
#include <stdlib.h>
#include <mpi.h>
#include "FeatureData.h"

using namespace std;

class StaticNode {
	public:
		int feature;
		float split;
		double label, loss;

		int m_infty, m_s;
		float s;		// data->getSortedFeature(f, j)
		double l_infty, l_s;

	public:
		void printNodes(){
			cout << "feature: " << feature << "\t"
				<< "split: " << split << "\t"
				<< "label: " << label << endl
				<< "loss: " << loss << "\t"
				<< "m_infty: " << m_infty << "\t"
				<< "m_s: " << m_s << endl
				<< "s: " << s << "\t"
				<< "l_infty: " << l_infty << "\t"
				<< "l_s: " << l_s << endl;
		}
};

class StaticTree {
	public:
		// constructor/destructor
		StaticTree(int depth); // creates a tree of fixed depth
		~StaticTree();

		// construction methods
		void clear();
		void startNextLayer();
		void findBestLocalSplits(FeatureData* data);
		void exchangeBestSplits();
		bool containsSplittingFeature(FeatureData* data);

		// prediction methods
		void updateTrainingPredictions(FeatureData *data, double learningrate);
		void updatePredictions(InstanceData *data, double learningrate);

		// output methods
		void printTree(double learningrate);

		// info methods
		void getSplit(int node, int &feature, float &split);
		int getNumNodes();

	private:
		StaticNode*** layers;
		int depth, layer, nodes;

		int nodesAtDepth(int d);
		void clearNode(StaticNode* node);
		double classifyDataPoint(InstanceData* data, int p);
		void updateBestSplits(FeatureData* data, int f);
		void updateBestSplits2(FeatureData* data, int f);

		void printNode(int level, int i, double learningrate);
};


StaticTree::StaticTree(int depth_) {
	// initialize parameters
	depth = depth_;
	layer = 0;
	nodes = 1;

	// initialize levels
	layers = new StaticNode**[depth];
	for (int i=0; i<depth; i++) {
		// initialize layer of nodes
		int n = (int) pow(2.f, (float) i);
		layers[i] = new StaticNode*[n];

		// initialize nodes
		for (int j=0; j<n; j++) {
			layers[i][j] = new StaticNode();
			StaticNode* node = layers[i][j];
			clearNode(node);
		}
	}
}

StaticTree::~StaticTree() {
	// delete histograms and nodes
	for (int i=0; i<depth; i++) {
		delete [] layers[i];
		layers[i] = NULL;
	}

	// delete layers
	delete [] layers;
}

void StaticTree::clear() {
	// clear each node
	for (int d=0; d<depth; d++) {
		int n = nodesAtDepth(d);
		for (int j=0; j<n; j++) {
			StaticNode* node = layers[d][j];
			clearNode(node);
		}
	}

	// reset layer
	layer = 0;
	nodes = 1;
}

void StaticTree::clearNode(StaticNode* node) {
	node->feature = -1;
	node->split = -1.f;
	node->label = -1.0;
	node->loss = -1.0;

	node->m_infty = 0;
	node->l_infty = 0.0;
}

void StaticTree::startNextLayer() {
	// increment layer
	layer++;
	nodes = (int) pow(2.f,((float) layer));
}

int StaticTree::nodesAtDepth(int d) {
	return (int) pow(2.f,((float) d));
}

void StaticTree::findBestLocalSplits(FeatureData* data) {
	// recompute counts at nodes
	//cout << "node:" << endl;
	for (int i=0; i<data->getN(); i++) {
		int n = data->getNode(i);
		//cout << n << " ";
		//if (i % 10 == 0)
		//	cout << endl;
		layers[layer][n]->m_infty += 1;
		layers[layer][n]->l_infty += data->getResidual(i);
	}

	// set labels at nodes
	// (this is redundant for many internal nodes -- 
	// overwriting the existing value with the same value --
	// but necessary for the root node and nodes that have stopped short)
	for (int n=0; n<nodes; n++) {
		StaticNode* node = layers[layer][n];
		StaticNode* child1 = layers[layer+1][n*2];
		StaticNode* child2 = layers[layer+1][n*2+1];
		if (node->m_infty > 0) {
			double label = node->l_infty / (double) node->m_infty;
			node->label = label;
			child1->label = label;
			child2->label = label;
		}
	}

	// iterate over features and update best splits at each node
	for (int f=0; f<data->getNumFeatures(); f++) {
		updateBestSplits(data, f);
	}
	/*
	for (int n = 0; n < nodes; n++) {
		StaticNode* node = layers[layer][n];
		cout << "------ " << node->split << " " << node->feature << endl;
		cout << node->m_s << " " << node->l_s << " " << node->m_infty << " " << node->l_infty << endl;
	}
	*/
}

void StaticTree::updateBestSplits(FeatureData* data, int f) {
	// compute global feature index
	int globalf = data->globalFeatureIndex(f);

	// reset counts at nodes
	for (int n=0; n<nodes; n++) {
		StaticNode* node = layers[layer][n];
		node->m_s = 0;
		node->l_s = 0.0;
		node->s = 0.0;
	}

	
	/*
	for (int n = 0; n < nodes; n++) {
		StaticNode* node = layers[layer][n];
		node->m_s = data->getN() - (*data).sortedfeatures[f].size();
	}
	*/
	bool* flag = new bool[(*data).getN()];

	memset(flag, 1, sizeof(bool) * (*data).getN());
	
	for(int ii = 0; ii < (*data).sortedfeatures[f].size(); ii++)
		flag[(*data).sortedfeatures[f][ii].i_index] = false;

	int ii = (*data).getN() - 1; 

	// iterate over feature
	for (int j=0; j<data->getN(); j++) {
		// get current value
		float v = data->getSortedFeature(f,j);

		if (v != 0)
			break;
		int i = data->getSortedIndex(f,j);
		
		if (i == -1) {
			for (; ii >= 0; ii--) {
				if (flag[ii]) {
					i = ii;		
					flag[i] = false;
					break;
				}	
			}
		}
		
		
		int n = data->getNode(i);
		
		float l = data->getResidual(i);
	
		StaticNode* node = layers[layer][n];

		// update variables
		node->m_s += 1;
		node->l_s += l;	
	}
	
	for (int j = 0; j < (*data).sortedfeatures[f].size(); j++) {
		float v = (*data).sortedfeatures[f][j].value;
		int i = (*data).sortedfeatures[f][j].i_index;

		int n = data->getNode(i);

		float l = data->getResidual(i);

		StaticNode* node = layers[layer][n];
		if (node->m_s > 0 and v > node->s) {
			double loss_i = pow(node->l_s,2.0) / (double) node->m_s + pow(node->l_infty - node->l_s,2.0) / (double) (node->m_infty - node->m_s);

			
			if (node->loss < 0 or loss_i > node->loss) {
				node->loss = loss_i;
				node->feature = globalf;
				node->split = (node->s + v) / 2.f;
				//cout << "split: " << node->s << " " << v << " " << node->split << endl;

				// TODO : create a lookup table for these child values at tree construction, store in static tree or store each in static node
				StaticNode* child1 = layers[layer+1][2*n];
				child1->label = node->l_s / (double) node->m_s;
				StaticNode* child2 = layers[layer+1][2*n+1];
				child2->label = (node->l_infty - node->l_s) / (double) (node->m_infty - node->m_s);

				// if (child2->label > 5.0)
				//     printf("### %f %d %d %f %d %f %d %f %d %d %d %f %d\n", v, i, n, l, node->m_s, node->l_s, node->m_infty, node->l_infty, globalf, f, j, loss_i, layer);
			}
			
		}

		// update variables
		node->m_s += 1;
		node->l_s += l;
		node->s = v;

	}

	//cout << endl;
	delete [] flag;
}

void StaticTree::exchangeBestSplits() {
	// instantiate buffer
	int buffersize = nodes*5;
	double* buffer = new double[buffersize];

	// write layer of tree to buffer
	for (int n=0; n<nodes; n++) {
		StaticNode* node = layers[layer][n];
		buffer[n*5 + 0] = node->loss;
		buffer[n*5 + 1] = node->feature;
		buffer[n*5 + 2] = node->split;

		StaticNode* child1 = layers[layer+1][n*2];
		buffer[n*5 + 3] = child1->label;

		StaticNode* child2 = layers[layer+1][n*2+1];
		buffer[n*5 + 4] = child2->label;
	}

	// get myid and numprocs
	int myid;
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	int numprocs;
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

	// determine isRoot
	int root = numprocs-1;
	bool isRoot = (myid == root);

	// exchange buffers
	double* rbuffer = (isRoot ? new double[numprocs*buffersize] : NULL);
	MPI_Gather(buffer, buffersize, MPI_DOUBLE, rbuffer, buffersize, MPI_DOUBLE, root, MPI_COMM_WORLD);

	// save best global splits
	if (isRoot) for (int n=0; n<nodes; n++) {
		// reset loss at node and get pointers
		StaticNode* node = layers[layer][n];
		node->loss = -1;
		StaticNode* child1 = layers[layer+1][n*2];
		StaticNode* child2 = layers[layer+1][n*2+1];

		// consider loss from all processors
		for (int p=0; p<numprocs; p++) {
			int offset = p*buffersize + n*5;
			double loss = rbuffer[offset + 0];

			// update if better than current
			if (node->loss < 0 or loss > node->loss) {
				node->loss = loss;
				node->feature = (int) rbuffer[offset + 1];
				node->split = (float) rbuffer[offset + 2];
				child1->label = rbuffer[offset + 3];
				child2->label = rbuffer[offset + 4];
			}
		}
	}

	// buffer best global splits
	if (isRoot) for (int n=0; n<nodes; n++) {
		StaticNode* node = layers[layer][n];
		buffer[n*5 + 0] = node->loss;
		buffer[n*5 + 1] = node->feature;
		buffer[n*5 + 2] = node->split;

		StaticNode* child1 = layers[layer+1][n*2];
		buffer[n*5 + 3] = child1->label;

		StaticNode* child2 = layers[layer+1][n*2+1];
		buffer[n*5 + 4] = child2->label;
	}

	// broadcast best splits
	MPI_Bcast(buffer, nodes*5, MPI_DOUBLE, root, MPI_COMM_WORLD);

	// update tree with best global splits
	for (int n=0; n<nodes; n++) {
		StaticNode* node = layers[layer][n];
		node->loss = buffer[n*5 + 0];
		node->feature = (int) buffer[n*5 + 1];
		node->split = (float) buffer[n*5 + 2];

		StaticNode* child1 = layers[layer+1][n*2];
		child1->label = buffer[n*5 + 3];

		StaticNode* child2 = layers[layer+1][n*2+1];
		child2->label = buffer[n*5 + 4];
	}

	// delete buffers
	delete [] buffer;
	delete [] rbuffer;
}

bool StaticTree::containsSplittingFeature(FeatureData* data) {
	int feature; float split;
	for (int i=0; i<nodes; i++) {
		getSplit(i, feature, split);
		if (data->isLocalFeature(feature)) return true;
	}
	return false;
}

void StaticTree::updateTrainingPredictions(FeatureData *data, double learningrate) {
	int N = data->getN();
	for (int i=0; i<N; i++) {
		int node = data->getNode(i);
		double pred = learningrate * layers[layer][node]->label;
		data->updatePred(i,pred);
	}
}

void StaticTree::updatePredictions(InstanceData *data, double learningrate) {
	int N = data->getN();
	for (int i=0; i<N; i++) {
		double pred = learningrate * classifyDataPoint(data, i);
		data->updatePred(i,pred);
	}
}

double StaticTree::classifyDataPoint(InstanceData* data, int p) {
	// descend tree
	int node = 0;
	for (int i=0; i<depth-1; i++) {
		// get feature and split point
		int f = layers[i][node]->feature;
		float s = layers[i][node]->split;

		// check for valid split, otherwise return
		if (f < 0) return layers[i][node]->label;

		// perform split
		node <<= 1; // node *= 2, index of left child
		node |= (data->getFeature(f,p) >= s); // node += 1, if right child
	}

	// return label of leaf node as prediction
	return layers[depth-1][node]->label;
}

void StaticTree::printNode(int level, int i, double learningrate) {
	// get node
	StaticNode *node = layers[level][i];

	// print node
	if (level > 0) printf(",");
	printf("%d:%f:%f", node->feature, node->split, learningrate * node->label);

	// print children
	if (node->feature > 0) { // a splitting node
		printNode(level+1, 2*i, learningrate); // print left child
		printNode(level+1, 2*i+1, learningrate); // print right child
	}
}

void StaticTree::printTree(double learningrate) {
	printNode(0,0,learningrate);
	printf("\n");
}

void StaticTree::getSplit(int node, int &feature, float &split) {
	feature = layers[layer][node]->feature;
	split = layers[layer][node]->split;
}

int StaticTree::getNumNodes() {
	return nodes;
}

#endif
