/*
 * Procesos/unidades.h
 *
 *  Conversión de pulgadas a milímetros: todo el proyecto trabaja en mm
 *  (diámetro de fresa, huelgo, pasos). Llamar justo después de parsear.
 */

#ifndef PROCESOS_UNIDADES_H_
#define PROCESOS_UNIDADES_H_

#include <variant>

#include "drill.h"
#include "gerber.h"

class Unidades {
public:
	static void convertirAMilimetros(gerber::GerberResult &r) {
		if (r.unit != "IN") {
			return;
		}
		const double k = 25.4;
		auto esc = [k](gerber::Vec2 &v) {
			v.x *= k;
			v.y *= k;
		};
		for (auto &p : r.primitives) {
			if (auto *s = std::get_if<gerber::Segment>(&p.data)) {
				esc(s->start);
				esc(s->end);
				s->width *= k;
			} else if (auto *a = std::get_if<gerber::Arc>(&p.data)) {
				esc(a->start);
				esc(a->end);
				esc(a->center);
				a->radius *= k;
				a->width *= k;
			} else if (auto *f = std::get_if<gerber::Flash>(&p.data)) {
				esc(f->pos);
				f->aperture.outer *= k;
				f->aperture.inner *= k;
				f->aperture.height *= k;
				f->aperture.cornerRadius *= k;
				f->aperture.centerX *= k;
				f->aperture.centerY *= k;
			} else if (auto *g = std::get_if<gerber::Region>(&p.data)) {
				for (auto &v : g->outline) {
					esc(v);
				}
				for (auto &h : g->holes) {
					for (auto &v : h) {
						esc(v);
					}
				}
			}
		}
		if (r.bounds.valid) {
			r.bounds.minX *= k;
			r.bounds.maxX *= k;
			r.bounds.minY *= k;
			r.bounds.maxY *= k;
		}
		r.unit = "MM";
	}

	static void convertirAMilimetros(drill::DrillResult &r) {
		if (r.unit != "IN") {
			return;
		}
		const double k = 25.4;
		for (auto &op : r.operations) {
			op.x *= k;
			op.y *= k;
			op.x2 *= k;
			op.y2 *= k;
			op.diameter *= k;
			op.unit = "MM";
		}
		r.unit = "MM";
	}
};

#endif /* PROCESOS_UNIDADES_H_ */

/*
 * Procesos/unidades.h
 *
 *  Conversión de pulgadas a milímetros: todo el proyecto trabaja en mm
 *  (diámetro de fresa, huelgo, pasos). Llamar justo después de parsear.
 */

//#ifndef PROCESOS_UNIDADES_H_
//#define PROCESOS_UNIDADES_H_
//
//#include <variant>
//
//#include "drill.h"
//#include "gerber.h"
//
//class Unidades {
//
//	inline void convertirAMilimetros(gerber::GerberResult &r) {
//		if (r.unit != "IN") {
//			return;
//		}
//		const double k = 25.4;
//		auto esc = [k](gerber::Vec2 &v) {
//			v.x *= k;
//			v.y *= k;
//		};
//		for (auto &p : r.primitives) {
//			if (auto *s = std::get_if<gerber::Segment>(&p.data)) {
//				esc(s->start);
//				esc(s->end);
//				s->width *= k;
//			} else if (auto *a = std::get_if<gerber::Arc>(&p.data)) {
//				esc(a->start);
//				esc(a->end);
//				esc(a->center);
//				a->radius *= k;
//				a->width *= k;
//			} else if (auto *f = std::get_if<gerber::Flash>(&p.data)) {
//				esc(f->pos);
//				f->aperture.outer *= k;
//				f->aperture.inner *= k;
//				f->aperture.height *= k;
//				f->aperture.cornerRadius *= k;
//				f->aperture.centerX *= k;
//				f->aperture.centerY *= k;
//			} else if (auto *g = std::get_if<gerber::Region>(&p.data)) {
//				for (auto &v : g->outline) {
//					esc(v);
//				}
//				for (auto &h : g->holes) {
//					for (auto &v : h) {
//						esc(v);
//					}
//				}
//			}
//		}
//		if (r.bounds.valid) {
//			r.bounds.minX *= k;
//			r.bounds.maxX *= k;
//			r.bounds.minY *= k;
//			r.bounds.maxY *= k;
//		}
//		r.unit = "MM";
//	}
//
//	inline void convertirAMilimetros(drill::DrillResult &r) {
//		if (r.unit != "IN") {
//			return;
//		}
//		const double k = 25.4;
//		for (auto &op : r.operations) {
//			op.x *= k;
//			op.y *= k;
//			op.x2 *= k;
//			op.y2 *= k;
//			op.diameter *= k;
//			op.unit = "MM";
//		}
//		r.unit = "MM";
//	}
//};
//
//#endif /* PROCESOS_UNIDADES_H_ */
