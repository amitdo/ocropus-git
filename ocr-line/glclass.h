// -*- C++ -*-

#ifndef glclass_h__
#define glclass_h__

#include <typeinfo>
#include <stdio.h>
#include "gliovecs.h"
#include "gldataset.h"

namespace {
    // compute a classmap that maps a set of possibly sparse classes onto a dense
    // list of new classes and vice versa

    void classmap(intarray &class_to_index,intarray &index_to_class,intarray &classes) {
        int nclasses = max(classes)+1;
        intarray hist(nclasses);
        hist.fill(0);
        for(int i=0;i<classes.length();i++) {
            if(classes(i)==-1) continue;
            hist(classes(i))++;
        }
        int count = 0;
        for(int i=0;i<hist.length();i++)
            if(hist(i)>0) count++;
        class_to_index.resize(nclasses);
        class_to_index.fill(-1);
        index_to_class.resize(count);
        index_to_class.fill(-1);
        int index=0;
        for(int i=0;i<hist.length();i++) {
            if(hist(i)>0) {
                class_to_index(i) = index;
                index_to_class(index) = i;
                index++;
            }
        }
        CHECK(class_to_index.length()==nclasses);
        CHECK(index_to_class.length()==max(class_to_index)+1);
        CHECK(index_to_class.length()<=class_to_index.length());
    }

    // unpack posteriors or discriminant values using a translation map
    // this is to go from the output of a classifier with a limited set
    // of classes to a full output vector

    void ctranslate_vec(floatarray &result,floatarray &input,intarray &translation) {
        result.resize(max(translation)+1);
        result.fill(0);
        for(int i=0;i<input.length();i++)
            result(translation(i)) = input(i);
    }

    void ctranslate_vec(floatarray &v,intarray &translation) {
        floatarray temp;
        ctranslate_vec(temp,v,translation);
        v.move(temp);
    }

    // translate classes using a translation map

    void ctranslate(intarray &values,intarray &translation) {
        for(int i=0;i<values.length();i++)
            values(i) = translation(values(i));
    }

    void ctranslate(intarray &result,intarray &values,intarray &translation) {
        result.resize(values.length());
        for(int i=0;i<values.length();i++) {
            int v = values(i);
            if(v<0) result(i) = v;
            else result(i) = translation(v);
        }
    }

    // count the number of distinct classes in a list of classes (also
    // works for a translation map

    int classcount(intarray &classes) {
        int nclasses = max(classes)+1;
        intarray hist(nclasses);
        hist.fill(0);
        for(int i=0;i<classes.length();i++) {
            if(classes(i)==-1) continue;
            hist(classes(i))++;
        }
        int count = 0;
        for(int i=0;i<hist.length();i++)
            if(hist(i)>0) count++;
        return count;
    }

}

namespace glinerec {

    struct IIncremental : IComponent {
        virtual const char *name() { return "IIncremental"; }
        virtual const char *interface() { return "IIncremental"; }
        virtual void add(floatarray &v,int c) = 0;
        virtual void updateModel() = 0;
        virtual float outputs(OutputVector &ov,floatarray &x) = 0;
        virtual void train(IDataset &ds) {
            floatarray v;
            for(int i=0;i<ds.nsamples();i++) {
                ds.input(v,i);
                add(v,ds.cls(i));
            }
        }

        // special inquiry functions

        virtual void copy(IModel &) { throw Unimplemented(); }
        virtual int nprotos() {return 0;}
        virtual void getproto(floatarray &v,int i,int variant) { throw Unimplemented(); }
        virtual int nmodels() { return 0; }
        virtual void setModel(IModel *,int i) { throw "no submodels"; }
        virtual IComponent &getModel(int i) { throw "no submodels"; }

    };

    struct IModel : IIncremental {
        virtual const char *name() { return "IModel"; }
        virtual const char *interface() { return "IModel"; }

        virtual int nfeatures() { return -1; }
        virtual int nclasses() { return -1; }
        virtual void train(IDataset &dataset) = 0;
        virtual float outputs(OutputVector &result,floatarray &v) = 0;

        // convenience functions

        virtual int classify(floatarray &v) {
            OutputVector p;
            outputs(p,v);
            return p.argmax();
        }

        // incremental training for batch models

        autodel<IExtDataset> ds;

        IModel() {
            pdef("cds","rowdataset8","default dataset buffer class");

        }

        virtual void add(floatarray &v,int c) {
            if(!ds) {
                debugf("info","allocating %s buffer for classifier\n",pget("cds"));
                make_component(pget("cds"),ds);
            }
            ds->add(v,c);
        }

        virtual void updateModel() {
            if(!ds) return;
            debugf("info","updateModel %d samples, %d features, %d classes\n",
                   ds->nsamples(),ds->nfeatures(),ds->nclasses());
            debugf("info","updateModel memory status %d Mbytes, %d Mvalues\n",
                   int((long long)sbrk(0)/1000000),
                   (ds->nsamples() * ds->nfeatures())/1000000);
            train(*ds);
            ds = 0;
        }
    };

    struct IModelDense : IModel {
        intarray c2i,i2c;

        IModelDense() {
            persist(c2i,"c2i");
            persist(i2c,"i2c");
        }

        float outputs(OutputVector &result,floatarray &v) {
            floatarray out;
            float cost = outputs_dense(out,v);
            result.clear();
            for(int i=0;i<out.length();i++)
                result(i2c(i)) = out(i);
            return cost;
        }

        struct TranslatedDataset : IDataset {
            IDataset &ds;
            intarray &c2i;
            int nc;
            const char *name() { return "mappeddataset"; }
            TranslatedDataset(IDataset &ds,intarray &c2i) : ds(ds),c2i(c2i) {
                nc = max(c2i)+1;
            }
            int nclasses() { return nc; }
            int nfeatures() { return ds.nfeatures(); }
            int nsamples() { return ds.nsamples(); }
            void input(floatarray &v,int i) { ds.input(v,i); }
            int cls(int i) { return c2i(ds.cls(i)); }
            int id(int i) { return ds.id(i); }
        };

        void train(IDataset &ds) {
            CHECK(ds.nsamples()>0);
            CHECK(ds.nfeatures()>0);
            if(c2i.length()<1) {
                intarray raw_classes;
                for(int i=0;i<ds.nsamples();i++)
                    raw_classes.push(ds.cls(i));
                classmap(c2i,i2c,raw_classes);
                intarray classes;
                ctranslate(classes,raw_classes,c2i);
                debugf("info","[mapped %d to %d classes]\n",c2i.length(),i2c.length());
            }
            TranslatedDataset mds(ds,c2i);
            train_dense(mds);
        }

        virtual void train_dense(IDataset &dataset) = 0;
        virtual float outputs_dense(floatarray &result,floatarray &v) = 0;
    };

    void confusion_matrix(intarray &confusion,IModel &classifier,floatarray &data,intarray &classes);
    void confusion_matrix(intarray &confusion,IModel &classifier,IDataset &ds);
    void confusion_print(intarray &confusion);
    int compute_confusions(intarray &list,intarray &confusion);
    void print_confusion(IModel &classifier,floatarray &vs,intarray &cs);
    float confusion_error(intarray &confusion);

    void least_square(floatarray &xf,floatarray &Af,floatarray &bf);

    inline IModel *make_model(const char *name) {
        IModel *result = dynamic_cast<IModel*>(component_construct(name));
        CHECK(result!=0);
        return result;
    }

    extern IRecognizeLine *current_recognizer_;
}

#endif
