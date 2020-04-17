﻿/*
		Copyright 2006-2020 QElectroTech Team
        This file is part of QElectroTech.

        QElectroTech is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 2 of the License, or
        (at your option) any later version.

        QElectroTech is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "qetgraphicstableitem.h"
#include "diagram.h"
#include "qetgraphicsheaderitem.h"
#include "QPropertyUndoCommand/qpropertyundocommand.h"
#include "qetxml.h"
#include "nomenclaturemodel.h"
#include "elementprovider.h"

#include <QAbstractItemModel>
#include <QFontMetrics>
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>

static int no_model_height = 20;
static int no_model_width = 40;

/**
 * @brief QetGraphicsTableItem::QetGraphicsTableItem
 * Default constructor
 * @param parent
 */
QetGraphicsTableItem::QetGraphicsTableItem(QGraphicsItem *parent) :
	QetGraphicsItem(parent)
{
	setFlag(QGraphicsItem::ItemIsMovable, true);
	setFlag(QGraphicsItem::ItemIsSelectable, true);
	setAcceptHoverEvents(true);
	setUpHandler();

	m_margin = QMargins(5,3,15,3);
		//A litle bounding rect before model is set,
		//then user can already grab this item, even if model is not already set
	m_bounding_rect.setRect(m_br_margin/-2, m_br_margin/-2, 50, 50);

	connect(this, &QetGraphicsTableItem::xChanged, this, &QetGraphicsTableItem::adjustHandlerPos);
	connect(this, &QetGraphicsTableItem::yChanged, this, &QetGraphicsTableItem::adjustHandlerPos);

	m_header_item = new QetGraphicsHeaderItem(this);
	connect(m_header_item, &QetGraphicsHeaderItem::sectionResized, this, &QetGraphicsTableItem::headerSectionResized);
	connect(m_header_item, &QetGraphicsHeaderItem::heightResized, this, [this]() {
		m_header_item->setPos(0, 0-m_header_item->rect().height());
	});
		//Init the size of table without a model
	setModel();
}

QetGraphicsTableItem::~QetGraphicsTableItem()
{}

/**
 * @brief QetGraphicsTableItem::setModel
 * Set the model presented by this item.
 * Since QetGraphicsTableItem don't take ownership of model,
 * if item already have a model, it's your responsibility to delete it.
 * @param model
 */
void QetGraphicsTableItem::setModel(QAbstractItemModel *model)
{
	if (m_model)
	{
		disconnect(m_model, &QAbstractItemModel::dataChanged, this, &QetGraphicsTableItem::dataChanged);
		disconnect(m_model, &QAbstractItemModel::modelReset, this, &QetGraphicsTableItem::modelReseted);
	}
	m_model = model;
	m_header_item->setModel(model);

	setUpColumnAndRowMinimumSize();
	adjustSize();

	m_header_item->setPos(0, -m_header_item->rect().height());
	if (m_model)
	{
		connect(m_model, &QAbstractItemModel::dataChanged, this, &QetGraphicsTableItem::dataChanged);
		connect(m_model, &QAbstractItemModel::modelReset, this, &QetGraphicsTableItem::modelReseted);
	}

	if (m_next_table) {
		m_next_table->setModel(m_model);
	}
}

/**
 * @brief QetGraphicsTableItem::model
 * @return The model that this item is presenting
 */
QAbstractItemModel *QetGraphicsTableItem::model() const {
	return m_model;
}

/**
 * @brief QetGraphicsTableItem::boundingRect
 * Reimplemented from QGraphicsObject
 * @return
 */
QRectF QetGraphicsTableItem::boundingRect() const {
	return m_bounding_rect;
}

/**
 * @brief QetGraphicsTableItem::paint
 * Draw the table
 * @param painter
 * @param option
 * @param widget
 */
void QetGraphicsTableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
	Q_UNUSED(option)
	Q_UNUSED(widget)

	painter->save();

	QPen pen;
	pen.setWidthF(0.7);
	pen.setColor(Qt::black);
	painter->setPen(pen);
	painter->drawRect(0,0, m_header_item->rect().width(), m_current_size.height());

	if (isSelected())
	{
		painter->save();
		QColor color(Qt::darkBlue);
		color.setAlpha(20);
		painter->setBrush(QBrush (color));
		painter->setPen(Qt::NoPen);
		painter->drawRect(0,0, m_header_item->rect().width(), m_current_size.height());
		painter->restore();
	}

	if(!m_model)
	{
		painter->restore();
		return;
	}
	painter->setFont(m_model->data(m_model->index(0,0), Qt::FontRole).value<QFont>());

		//Draw vertical lines
	auto offset= 0;
	for(auto i=0 ; i<m_model->columnCount() ; ++i)
	{
		QPointF p1(offset+m_header_item->sectionSize(i), 0);
		QPointF p2(offset+m_header_item->sectionSize(i), m_current_size.height());
		painter->drawLine(p1, p2);
		offset += m_header_item->sectionSize(i);
	}

		//Calcule the number of rows to display.
	auto row_count = m_model->rowCount();

	if (m_previous_table) //Remove the number of row already displayed by previous tables
		row_count -= m_previous_table->displayNRowOffset();

	if (m_number_of_displayed_row > 0) //User override the number of row to display
		row_count = std::min(row_count, m_number_of_displayed_row);

		//Draw horizontal lines
	auto cell_height =  static_cast<double>(m_current_size.height())/static_cast<double>(row_count);
	for(auto i= 1 ; i-1<row_count ; ++i)
	{
		QPointF p1(m_header_item->rect().left(), cell_height*i);
		QPointF p2(m_header_item->rect().right(), cell_height*i);
		painter->drawLine(p1, p2);
	}

		//Write text of each cell
	for (auto i=0 ; i<row_count ; ++i)
	{
		QPointF top_left(m_margin.left(), i==0? m_margin.top() : cell_height*i + m_margin.top());

		for(auto j= 0 ; j<m_model->columnCount() ; ++j)
		{
				//In first iteration the top left X is margin left, in all other iteration the top left X is stored in m_column_size
			if (j>0) {
				top_left.setX(top_left.x() + m_header_item->sectionSize(j-1));
			}
			QSize size(m_header_item->sectionSize(j) - m_margin.left() - m_margin.right(),
					   static_cast<int>(cell_height) - m_margin.top() - m_margin.bottom());
			auto index_row = m_previous_table ? i + m_previous_table->displayNRowOffset() : i;
			painter->drawText(QRectF(top_left, size),
							  m_model->data(m_model->index(0,0), Qt::TextAlignmentRole).toInt(),
							  m_model->index(index_row, j).data().toString());
		}
	}

	painter->restore();
}

/**
 * @brief QetGraphicsTableItem::setMargins
 * @param margins
 */
void QetGraphicsTableItem::setMargins(const QMargins &margins)
{
	m_margin = margins;
	setUpColumnAndRowMinimumSize();
	adjustSize();
	update();
}

/**
 * @brief QetGraphicsTableItem::setSize
 * Set the current size of the table to @size
 * @param size
 */
void QetGraphicsTableItem::setSize(const QSize &size)
{
	qDebug() << "ici";
	auto new_size = size;
	if (new_size.width() < minimumSize().width())  {
		new_size.setWidth(minimumSize().width());
	}
	if (new_size.height() < minimumSize().height()) {
		new_size.setHeight(minimumSize().height());
	}

	prepareGeometryChange();
	m_current_size = new_size;
	adjustColumnsWidth();
	setUpBoundingRect();
}

/**
 * @brief QetGraphicsTableItem::size
 * @return The current size of the table
 */
QSize QetGraphicsTableItem::size() const
{
	QSize size_(m_header_item->rect().width(), m_current_size.height());
	return size_;
}

/**
 * @brief QetGraphicsTableItem::minimumSize
 * @return the minimum size the table can be
 * The returned size take care of the table's minimum width, but also the header item's minimum width
 */
QSize QetGraphicsTableItem::minimumSize() const
{
	if (!m_model) {
		return QSize(no_model_width, no_model_height);
	}

	auto row_count = m_model->rowCount();
	if (m_previous_table) {
		row_count -= m_previous_table->displayNRowOffset();
	}

	if (m_number_of_displayed_row > 0)
		row_count = std::min(row_count, m_number_of_displayed_row);



	QSize s(std::accumulate(m_minimum_column_width.begin(), m_minimum_column_width.end(), 0), m_minimum_row_height*row_count);
	return s;
}

/**
 * @brief QetGraphicsTableItem::setDisplayNRow
 * Limit the number of row to display
 * @param number : set to 0 or less to disabled the limit of row to display
 */
void QetGraphicsTableItem::setDisplayNRow(const int &number) {
	m_number_of_displayed_row = number;
	setToMinimumHeight();
	if (m_next_table)
		m_next_table->previousTableDisplayRowChanged();
}

/**
 * @brief QetGraphicsTableItem::displayNRow
 * @return the number of row displayed.
 * A value of 0 or less mean there is no limit
 */
int QetGraphicsTableItem::displayNRow() const {
	return m_number_of_displayed_row;
}

/**
 * @brief QetGraphicsTableItem::setPreviousTable
 * Set the previous table to @table.
 * If this table already have a previous table, the previous table will be replaced.
 * Set new table to nullptr to remove an existing previous table.
 * The table uses the model of the new previous table.
 * Since the table does not take ownership of the model, it is your responsibility to manage the old model.
 * Linked tables (table with next and/or previous table) share the same model, a table always take the model of the previous table..
 * When remove a previous table (set to nullptr) from a table, the model is also removed, you need to set a new model
 * @param table
 */
void QetGraphicsTableItem::setPreviousTable(QetGraphicsTableItem *table)
{
	if (m_previous_table == table ||
		this == table) {
		return;
	}

	auto old_previous_table = m_previous_table;
	m_previous_table = table;
	if (m_previous_table) //set previous table and get her model
	{
		m_previous_table->setNextTable(this);
		setModel(m_previous_table->m_model);
	}
	else	//Remove model
	{
		setModel(nullptr);
	}

	if (old_previous_table &&
		old_previous_table->nextTable() == this) {
		old_previous_table->setNextTable(nullptr);
	}
}

/**
 * @brief QetGraphicsTableItem::setNextTable
 * Set the next table to @table
 * nullptr will remove an existing next table.
 * @param table
 */
void QetGraphicsTableItem::setNextTable(QetGraphicsTableItem *table)
{
	if (m_next_table == table ||
		this == table) {
		return;
	}

	auto old_next_table = m_next_table;

	m_next_table = table;
	if (m_next_table) {
		m_next_table->setPreviousTable(this);
	}

	if (old_next_table &&
		old_next_table->previousTable() == this) {
		old_next_table->setPreviousTable(nullptr);
	}

}

void QetGraphicsTableItem::setTableName(const QString &name) {
	m_name = name;
}

QString QetGraphicsTableItem::tableName() const {
	return m_name;
}

/**
 * @brief QetGraphicsTableItem::displayNRowOffset
 * @return the offset (aka the last displayed row) of displayed row.
 * If this item have a previous table, the previous offset is added.
 */
int QetGraphicsTableItem::displayNRowOffset() const
{
	auto offset_ = m_number_of_displayed_row;
	if(m_previous_table)
		offset_ += m_previous_table->displayNRowOffset();

	return offset_;
}

QetGraphicsTableItem *QetGraphicsTableItem::previousTable() const {
	return m_previous_table;
}

QetGraphicsTableItem *QetGraphicsTableItem::nextTable() const {
	return m_next_table;
}

/**
 * @brief QetGraphicsTableItem::setToMinimumHeight
 * Set the height to the the minimum.
 * The width stay unchanged.
 */
void QetGraphicsTableItem::setToMinimumHeight()
{
	auto size_ = size();
	size_.setHeight(1);
	setSize(size_);
}

void QetGraphicsTableItem::initLink()
{
	if (!m_pending_previous_table_uuid.isNull())
	{
		ElementProvider provider_(this->diagram());
		if (auto previous_table = provider_.tableFromUuid(m_pending_previous_table_uuid)) {
			setPreviousTable(previous_table);
		}
		m_pending_previous_table_uuid = QUuid(); //Set to null in case initLink is called again
	}
	setSize(m_pending_size);
}

/**
 * @brief QetGraphicsTableItem::toXml
 * Save the table to xml
 * @param dom_document : parent document
 * @return the dom_element that describe the table
 */
QDomElement QetGraphicsTableItem::toXml(QDomDocument &dom_document) const
{
	auto dom_table = dom_document.createElement(xmlTagName());
	dom_table.setAttribute("x", QString::number(pos().x()));
	dom_table.setAttribute("y", QString::number(pos().y()));
	dom_table.setAttribute("width", QString::number(m_current_size.width()));
	dom_table.setAttribute("height", QString::number(m_current_size.height()));
	dom_table.setAttribute("uuid", m_uuid.toString());
	dom_table.setAttribute("name", m_name);
	dom_table.setAttribute("display_n_row", QString::number(m_number_of_displayed_row));

		//Add the header xml
	dom_table.appendChild(m_header_item->toXml(dom_document));

		//Add previous table, the model is save by the previous table
	if (m_previous_table)
	{
		auto dom_previous_table = dom_document.createElement("previous_table");
		dom_previous_table.setAttribute("uuid", m_previous_table->m_uuid.toString());
		dom_table.appendChild(dom_previous_table);
	}
	else if (m_model) //There is not a previous table, we need to save the model
	{
			//Add cell properties
		auto dom_cell = dom_document.createElement("cell");
		dom_cell.setAttribute("font", m_model->data(m_model->index(0,0), Qt::FontRole).toString());
		auto me = QMetaEnum::fromType<Qt::Alignment>();
		dom_cell.setAttribute("alignment", me.valueToKey(m_model->data(m_model->index(0,0), Qt::TextAlignmentRole).toInt()));
		dom_cell.appendChild(QETXML::marginsToXml(dom_document, m_margin));
		dom_table.appendChild(dom_cell);

			//Add model
		auto dom_model = dom_document.createElement("model");
		auto nomenclature_model = static_cast<NomenclatureModel *>(m_model);
		dom_model.appendChild(nomenclature_model->toXml(dom_document));
		dom_table.appendChild(dom_model);

	}

	return dom_table;
}

/**
 * @brief QetGraphicsTableItem::fromXml
 * Restore the table from xml.
 * Make this item is already in a diagram to
 * @param dom_element
 */
void QetGraphicsTableItem::fromXml(const QDomElement &dom_element)
{
	if (dom_element.tagName() != xmlTagName()) {
		return;
	}

	this->setPos(dom_element.attribute("x", QString::number(10)).toDouble(),
				 dom_element.attribute("y", QString::number(10)).toDouble());
		//Size is not set now because will change during the whole process of opening a project from the xml
	m_pending_size = QSize(dom_element.attribute("width",  QString::number(no_model_width)).toInt(),
						   dom_element.attribute("height", QString::number(no_model_height)).toInt());

	m_uuid = QUuid(dom_element.attribute("uuid", QUuid::createUuid().toString()));
	m_name = dom_element.attribute("name");
	m_number_of_displayed_row = dom_element.attribute("display_n_row", QString::number(0)).toInt();
	m_margin = QETXML::marginsFromXml(dom_element.firstChildElement("margins"));

	auto vector_ = QETXML::directChild(dom_element, "previous_table");
	if (vector_.size()) { //Table have a previous table
		m_pending_previous_table_uuid = QUuid(vector_.first().attribute("uuid"));
	}
	else if (this->diagram()) //The table haven't got a previous table, so there should be a model save to xml
	{
			//Get table
		auto model_ = new NomenclatureModel(this->diagram()->project(), this->diagram()->project());
		model_->fromXml(dom_element.firstChildElement("model").firstChildElement(NomenclatureModel::xmlTagName()));
		this->setModel(model_);

			//Get cell properties
		auto dom_cell = dom_element.firstChildElement("cell");
			//font
		QFont font_;
		font_.fromString(dom_cell.attribute("font"));
		m_model->setData(m_model->index(0,0), font_, Qt::FontRole);
			//alignment
		auto me = QMetaEnum::fromType<Qt::Alignment>();
		m_model->setData(m_model->index(0,0), me.keyToValue(dom_cell.attribute("alignment").toStdString().data()));
		dom_cell.setAttribute("alignment", me.valueToKey(m_model->data(m_model->index(0,0), Qt::TextAlignmentRole).toInt()));
			//margins
		m_margin =  QETXML::marginsFromXml(dom_cell.firstChildElement("margins"));
	}

		//Restore the header from xml
	m_header_item->fromXml(dom_element.firstChildElement(QetGraphicsHeaderItem::xmlTagName()));
}

/**
 * @brief QetGraphicsTableItem::hoverEnterEvent
 * Reimplemented from QetGraphicsItem
 * @param event
 */
void QetGraphicsTableItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	if (m_model)
	{
		adjustHandlerPos();
		this->scene()->addItem(&m_handler_item);
		m_handler_item.installSceneEventFilter(this);
	}
	QGraphicsObject::hoverEnterEvent(event);
}

/**
 * @brief QetGraphicsTableItem::hoverLeaveEvent
 * Reimplemented from QetGraphicsItem
 * @param event
 */
void QetGraphicsTableItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	if (m_model) {
		m_handler_item.scene()->removeItem(&m_handler_item);
	}
	QGraphicsObject::hoverLeaveEvent(event);
}

/**
 * @brief QetGraphicsTableItem::sceneEventFilter
 * Reimplemented from QetGraphicsItem
 * @param watched
 * @param event
 * @return
 */
bool QetGraphicsTableItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
	if (watched == &m_handler_item)
	{
		if(event->type() == QEvent::GraphicsSceneMousePress) //Click
		{
			handlerMousePressEvent(static_cast<QGraphicsSceneMouseEvent *>(event));
			return true;
		}
		else if(event->type() == QEvent::GraphicsSceneMouseMove) //Move
		{
			handlerMouseMoveEvent(static_cast<QGraphicsSceneMouseEvent *>(event));
			return true;
		}
		else if (event->type() == QEvent::GraphicsSceneMouseRelease) //Release
		{
			handlerMouseReleaseEvent(static_cast<QGraphicsSceneMouseEvent *>(event));
			return true;
		}
	}

	return false;
}

void QetGraphicsTableItem::modelReseted() {
	dataChanged(m_model->index(0,0), m_model->index(0,0), QVector<int>());
	setToMinimumHeight();
}

/**
 * @brief QetGraphicsTableItem::setUpColumnAndRowMinimumSize
 * Calcule the minimum row height and the minimum column width for each columns
 * this function doesn't change the geometry of the table.
 */
void QetGraphicsTableItem::setUpColumnAndRowMinimumSize()
{
	if (!m_model)
	{
		m_minimum_row_height = no_model_height;
		m_minimum_column_width = m_header_item->minimumSectionWidth();
		return;
	}

	QFontMetrics metrics(m_model->data(model()->index(0,0), Qt::FontRole).value<QFont>());
		//Set the height of row;
	m_minimum_row_height = metrics.boundingRect("HEIGHT TEST").height() + m_margin.top() + m_margin.bottom();

	m_minimum_column_width = m_header_item->minimumSectionWidth();

		//Get the maximum width of each columns
	for (auto row= 0 ; row<m_model->rowCount() ; ++row)
	{
		for(auto col= 0 ; col<m_model->columnCount() ; ++col)
		{
			auto index = m_model->index(row, col);
			auto width = metrics.boundingRect(index.data().toString()).width();
			m_minimum_column_width.replace(col, std::max(m_minimum_column_width.at(col), width + m_margin.left() + m_margin.right()));
		}
	}
}

/**
 * @brief QetGraphicsTableItem::setUpBoundingRect
 */
void QetGraphicsTableItem::setUpBoundingRect()
{
	QSize header_size = m_header_item->rect().size();
	QRect rect(0, -header_size.height(), header_size.width(), m_current_size.height() + header_size.height());
	m_bounding_rect = rect.adjusted(-m_br_margin, -m_br_margin, m_br_margin, m_br_margin);
}

/**
 * @brief QetGraphicsTableItem::adjustHandlerPos
 * Adjust the pos of the handler item
 */
void QetGraphicsTableItem::adjustHandlerPos() {
	m_handler_item.setPos(mapToScene(QRect(QPoint(0,0), size()).bottomRight()));
}

/**
 * @brief QetGraphicsTableItem::setUpHandler
 */
void QetGraphicsTableItem::setUpHandler()
{
	m_handler_item.setColor(Qt::blue);
	m_handler_item.setZValue(this->zValue() + 1);
}

void QetGraphicsTableItem::handlerMousePressEvent(QGraphicsSceneMouseEvent *event)
{
	Q_UNUSED(event)
	diagram()->clearSelection();
	this->setSelected(true);
	m_old_size = size();
		//User start to resize the table, disconnect the signal to avoid double paint.
	disconnect(m_header_item, &QetGraphicsHeaderItem::sectionResized, this, &QetGraphicsTableItem::headerSectionResized);
}

void QetGraphicsTableItem::handlerMouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
	auto new_handler_pos = Diagram::snapToGrid(event->scenePos());
	QSize size_ = QRectF(QPointF(0,0), mapFromScene(new_handler_pos)).size().toSize();

	QPoint new_pos(std::max(minimumSize().width(), size_.width()),
				   std::max(minimumSize().height(), size_.height()));
	m_handler_item.setPos(mapToScene(new_pos));

	QSize new_size(new_pos.x(), new_pos.y());
	if (new_size != size()) {
		setSize(new_size);
	}
}

void QetGraphicsTableItem::handlerMouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
	Q_UNUSED(event)
	if (diagram())
	{
		auto undo = new QPropertyUndoCommand(this, "size", m_old_size, size());
		undo->setAnimated();
		undo->setText(tr("Modifier la géometrie d'un tableau"));
		diagram()->undoStack().push(undo);
	}
		//User finish to resize the table, we can reconnect now
	connect(m_header_item, &QetGraphicsHeaderItem::sectionResized, this, &QetGraphicsTableItem::headerSectionResized);
}

/**
 * @brief QetGraphicsTableItem::adjustColumnsWidth
 * Adjust the size of each column according to the current table width by setting the sectionSize of the header item
 */
void QetGraphicsTableItem::adjustColumnsWidth()
{
	if (!m_model)
	{
		auto h_  = m_header_item->minimumSectionWidth();
		for (auto i=0 ; i<h_.size() ; ++i) {
			m_header_item->resizeSection(i, h_.at(i));
		}
		return;
	}

	auto a = m_current_size.width() - minimumSize().width();
	auto b = a/std::max(1,m_model->columnCount()); //avoid divide by 0

	for(auto i= 0 ; i<m_model->columnCount() ; ++i) {
		m_header_item->resizeSection(i, std::max(m_minimum_column_width.at(std::min(m_minimum_column_width.size()-1, i)),
												 m_header_item->minimumSectionWidth().at(std::min(m_header_item->minimumSectionWidth().size()-1, i))) + b);
	}
}

void QetGraphicsTableItem::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
	Q_UNUSED(topLeft)
	Q_UNUSED(bottomRight)
	Q_UNUSED(roles)

	auto size_ = size();
	setUpColumnAndRowMinimumSize();
	adjustSize();
	setSize(size_);
}

/**
 * @brief QetGraphicsTableItem::headerSectionResized
 * Connected to the header signal QetGraphicsTableItem sectionResized
 */
void QetGraphicsTableItem::headerSectionResized()
{
	auto header_size = m_header_item->rect().size();
	auto size_ = size();
	size_.setWidth(header_size.width());

	m_current_size = size_;
	prepareGeometryChange();
	setUpBoundingRect();
}

/**
 * @brief QetGraphicsTableItem::adjustSize
 * If needed, this function resize the current height and width of table
 * according to there minimum
 */
void QetGraphicsTableItem::adjustSize()
{
		//If there is no model, set the size to minimum
	if (!m_model) {
		setSize(minimumSize());
		return;
	}

	if (m_current_size.height() < minimumSize().height())
	{
		prepareGeometryChange();
		m_current_size.setHeight(minimumSize().height());
		setUpBoundingRect();
		update();
	}

	if (m_current_size.width() < minimumSize().width())
	{
		prepareGeometryChange();
		m_current_size.setWidth(minimumSize().width());
		adjustColumnsWidth();
		setUpBoundingRect();
		update();
	}
}

void QetGraphicsTableItem::previousTableDisplayRowChanged() {
	setToMinimumHeight();
	if (m_next_table) {
		m_next_table->previousTableDisplayRowChanged();
	}
}
