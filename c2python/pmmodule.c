/*
 * pmmodule.c
 *
 * C module to access pm data (preemption and migration ovd and length)
 * from python.
 *
 * The rationale is to process the bulk of a sample set using C, then
 * move only the interesting values to Python.
 */
#include <Python.h>
#include <numpy/arrayobject.h>

#include "pm_common.h"

static struct ovd_plen *preempt = NULL;
static struct ovd_plen *samel2 = NULL;
static struct ovd_plen *samechip = NULL;
static struct ovd_plen *offchip = NULL;

static int pcount = 0;
static int l2count = 0;
static int chipcount = 0;
static int offcount = 0;

static int loaded(int set)
{
	static int load = 0;
	if(!load && set)
		load = 1;

	return load;
}

static void cleanup()
{
	free(preempt);
	if(samel2)
		free(samel2);
	free(samechip);
	free(offchip);
}

/*
 * pm_load:	load raw data from filename and process overheads
 * 		for hierarchy of up to L3 cache levels
 *
 * @filename:		raw data file
 * @cores_per_l2:	number of cores that share an L2 cache
 * 			if (cores_per_l2 == 0) then all cores in a chip share
 * 			the L2 cache (i.e., no L3)
 * @num_phys_cpu:	number of physical sockets
 *
 * TODO this should be (re)integrated at some point, to allow NUMA / other
 * 	topologies evaluations
 * @cores_per_chip:	number of cores per chip (they can share a L3 or a L2
 * 			cache. This is decided by the cores_per_l2 value)
 */
static PyObject* pm_load(PyObject *self, PyObject *args)
{
	const char *filename;
	unsigned int cores_per_l2;
	unsigned int num_phys_cpu;
	int wss;
	int tss;

	struct full_ovd_plen *full_costs = NULL;
	int num_samples;

	if (!PyArg_ParseTuple(args, "sIIii", &filename, &cores_per_l2,
				&num_phys_cpu, &wss, &tss))
		return NULL;

	/* get valid overheads from raw file */
	if ((num_samples = get_valid_ovd(filename, &full_costs, wss, tss)) < 0)
		goto err;

	if ((preempt = malloc(num_samples * sizeof(struct ovd_plen))) < 0)
		goto err;

	memset(preempt, 0, num_samples * sizeof(struct ovd_plen));

	if (cores_per_l2) {
		if((samel2 = malloc(num_samples * sizeof(struct ovd_plen))) < 0)
			goto err_samel2;

		memset(samel2, 0, num_samples * sizeof(struct ovd_plen));
	}

	if((samechip = malloc(num_samples * sizeof(struct ovd_plen))) < 0)
		goto err_samechip;

	memset(samechip, 0, num_samples * sizeof(struct ovd_plen));

	if((offchip = malloc(num_samples * sizeof(struct ovd_plen))) < 0)
		goto err_offchip;

	memset(offchip, 0, num_samples * sizeof(struct ovd_plen));

	/* get p/m overheads and lengths */
	get_ovd_plen_umaxeon(full_costs, num_samples, cores_per_l2, num_phys_cpu,
		 preempt, &pcount, samel2, &l2count, samechip, &chipcount,
		 offchip, &offcount);

	loaded(1);

	free(full_costs);
	Py_INCREF(Py_None);
	return Py_None;

err_offchip:
	free(samechip);
err_samechip:
	if(cores_per_l2)
		free(samel2);
err_samel2:
	free(preempt);
err:
	free(full_costs);
	PyErr_Format(PyExc_ValueError, "Cannot load / analyze raw data");
	return NULL;
}

/* return the preemption (ovd, length) NumPy array with shape (pcount,2) */
static PyObject* pm_get_preemption(PyObject *self, PyObject *args)
{
	PyArrayObject *py_preempt;
	npy_intp shape[2];

	int i;
	long long *tmp;

	if (!loaded(0)) {
		PyErr_Format(PyExc_ValueError, "pm not Loaded!");
		return NULL;
	}

	shape[0] = pcount;
	shape[1] = 2;

	if (!PyArg_ParseTuple(args,""))
		return NULL;

	py_preempt = (PyArrayObject *) PyArray_SimpleNew(2,shape,NPY_LONGLONG);
	if (!py_preempt)
		goto err_alloc;

	for (i = 0; i < pcount; i++) {
		tmp = (long long *) PyArray_GETPTR2(py_preempt, i, 0);
		if(!tmp)
			goto err;

		*tmp = (long long) preempt[i].ovd;

		tmp = (long long *) PyArray_GETPTR2(py_preempt, i, 1);
		if(!tmp)
			goto err;

		*tmp = (long long) preempt[i].plen;
	}

	free(preempt);
	return (PyObject *) py_preempt;

err:
	PyArray_free(py_preempt);
err_alloc:
	PyErr_Format(PyExc_ValueError, "pm_get_preemption Error");
	cleanup();
	return NULL;
}

/* return the samel2 (ovd, length) NumPy array with shape (l2count,2) */
static PyObject* pm_get_samel2(PyObject *self, PyObject *args)
{
	PyArrayObject *py_samel2;
	npy_intp shape[2];

	int i;
	long long *tmp;

	if (!loaded(0)) {
		PyErr_Format(PyExc_ValueError, "pm not Loaded!");
		return NULL;
	}

	shape[0] = l2count;
	shape[1] = 2;

	if (!PyArg_ParseTuple(args,""))
		return NULL;

	py_samel2 = (PyArrayObject *) PyArray_SimpleNew(2,shape,NPY_LONGLONG);
	if (!py_samel2)
		goto err_alloc;

	for (i = 0; i < l2count; i++) {
		tmp = (long long *) PyArray_GETPTR2(py_samel2, i, 0);
		if(!tmp)
			goto err;

		*tmp = (long long) samel2[i].ovd;

		tmp = (long long *) PyArray_GETPTR2(py_samel2, i, 1);
		if(!tmp)
			goto err;

		*tmp = (long long) samel2[i].plen;
	}

	free(samel2);
	return (PyObject *) py_samel2;

err:
	PyArray_free(py_samel2);
err_alloc:
	PyErr_Format(PyExc_ValueError, "pm_get_preemption Error");
	cleanup();
	return NULL;
}

/* return the samechip (ovd, length) NumPy array with shape (chipcount,2) */
static PyObject* pm_get_samechip(PyObject *self, PyObject *args)
{
	PyArrayObject *py_samechip;
	npy_intp shape[2];

	int i;
	long long *tmp;

	if (!loaded(0)) {
		PyErr_Format(PyExc_ValueError, "pm not Loaded!");
		return NULL;
	}

	shape[0] = chipcount;
	shape[1] = 2;

	if (!PyArg_ParseTuple(args,""))
		return NULL;

	py_samechip = (PyArrayObject *) PyArray_SimpleNew(2,shape,NPY_LONGLONG);
	if (!py_samechip)
		goto err_alloc;

	for (i = 0; i < chipcount; i++) {
		tmp = (long long *) PyArray_GETPTR2(py_samechip, i, 0);
		if(!tmp)
			goto err;

		*tmp = (long long) samechip[i].ovd;

		tmp = (long long *) PyArray_GETPTR2(py_samechip, i, 1);
		if(!tmp)
			goto err;

		*tmp = (long long) samechip[i].plen;
	}

	free(samechip);
	return (PyObject *) py_samechip;

err:
	PyArray_free(py_samechip);
err_alloc:
	PyErr_Format(PyExc_ValueError, "pm_get_preemption Error");
	cleanup();
	return NULL;
}

/* return the offchip (ovd, length) NumPy array with shape (offcount,2) */
static PyObject* pm_get_offchip(PyObject *self, PyObject *args)
{
	PyArrayObject *py_offchip;
	npy_intp shape[2];

	int i;
	long long *tmp;

	if (!loaded(0)) {
		PyErr_Format(PyExc_ValueError, "pm not Loaded!");
		return NULL;
	}

	shape[0] = offcount;
	shape[1] = 2;

	if (!PyArg_ParseTuple(args,""))
		return NULL;

	py_offchip = (PyArrayObject *) PyArray_SimpleNew(2,shape,NPY_LONGLONG);
	if (!py_offchip)
		goto err_alloc;

	for (i = 0; i < offcount; i++) {
		tmp = (long long *) PyArray_GETPTR2(py_offchip, i, 0);
		if(!tmp)
			goto err;

		*tmp = (long long) offchip[i].ovd;

		tmp = (long long *) PyArray_GETPTR2(py_offchip, i, 1);
		if(!tmp)
			goto err;

		*tmp = (long long) offchip[i].plen;
	}

	free(offchip);
	return (PyObject *) py_offchip;

err:
	PyArray_free(py_offchip);
err_alloc:
	PyErr_Format(PyExc_ValueError, "pm_get_preemption Error");
	cleanup();
	return NULL;
}

static PyMethodDef PmMethods[] = {
	{"load", pm_load, METH_VARARGS, "Load data from raw files"},
	{"getPreemption", pm_get_preemption, METH_VARARGS,
		"Get preemption overheads - length"},
	{"getL2Migration", pm_get_samel2, METH_VARARGS,
		"Get L2 Migration overheads - length"},
	{"getOnChipMigration", pm_get_samechip, METH_VARARGS,
		"Get Chip (L2 or L3) overheads - length"},
	{"getOffChipMigration", pm_get_offchip, METH_VARARGS,
		"Get Off Chip overheads - length"},
	{NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initpm(void)
{
	PyObject *pm;
	pm = Py_InitModule("pm", PmMethods);
	if(!pm)
		return;

	/* required by NumPy */
	import_array();
}

