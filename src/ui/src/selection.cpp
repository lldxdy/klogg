/*
 * Copyright (C) 2010, 2013 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

// This file implements Selection.
// This class implements the selection handling. No check is made on
// the validity of the selection, it must be handled by the caller.
// There are three types of selection, only one type might be active
// at any time.

#include <numeric>

#include "abstractlogdata.h"
#include "linetypes.h"
#include "log.h"
#include "selection.h"

Selection::Selection()
{
    selectedPartial_.startColumn = 0;
    selectedPartial_.endColumn = 0;

    selectedRange_.endLine = 0_lnum;
}

void Selection::selectPortion( LineNumber line, int start_column, int end_column )
{
    // First unselect any whole line or range
    selectedLine_ = {};
    selectedRange_.startLine = {};

    selectedPartial_.line = line;
    selectedPartial_.startColumn = qMin( start_column, end_column );
    selectedPartial_.endColumn = qMax( start_column, end_column );
}

void Selection::selectRange( LineNumber start_line, LineNumber end_line )
{
    // First unselect any whole line and portion
    selectedLine_ = {};
    selectedPartial_.line = {};

    selectedRange_.startLine = qMin( start_line, end_line );
    selectedRange_.endLine = qMax( start_line, end_line );

    selectedRange_.firstLine = start_line;
}

void Selection::selectRangeFromPrevious( LineNumber line )
{
    LineNumber previous_line;

    if ( selectedLine_.has_value() )
        previous_line = *selectedLine_;
    else if ( selectedRange_.startLine.has_value() )
        previous_line = selectedRange_.firstLine;
    else if ( selectedPartial_.line.has_value() )
        previous_line = *selectedPartial_.line;
    else
        previous_line = 0_lnum;

    selectRange( previous_line, line );
}

void Selection::crop( LineNumber last_line )
{
    if ( selectedLine_.has_value() && *selectedLine_ > last_line )
        selectedLine_ = {};

    if ( selectedPartial_.line.has_value() && *selectedPartial_.line > last_line )
        selectedPartial_.line = {};

    if ( selectedRange_.endLine > last_line )
        selectedRange_.endLine = last_line;

    if ( selectedRange_.startLine.has_value() && *selectedRange_.startLine > last_line )
        selectedRange_.startLine = last_line;
}

Portion Selection::getPortionForLine( LineNumber line ) const
{
    if ( selectedPartial_.line.has_value() && *selectedPartial_.line == line ) {
        return Portion( *selectedPartial_.line, selectedPartial_.startColumn,
                        selectedPartial_.endColumn );
    }

    return {};
}

bool Selection::isLineSelected( LineNumber line ) const
{
    if ( selectedLine_.has_value() && line == *selectedLine_ )
        return true;
    else if ( selectedRange_.startLine.has_value() )
        return ( ( line >= *selectedRange_.startLine ) && ( line <= selectedRange_.endLine ) );
    else
        return false;
}

bool Selection::isPortionSelected( LineNumber line, int startColumn, int endColumn ) const
{
    if ( isLineSelected( line ) ) {
        return true;
    }

    const auto portion = getPortionForLine( line );
    if ( !portion.isValid() ) {
        return false;
    }

    return startColumn >= portion.startColumn() && endColumn <= portion.endColumn();
}

OptionalLineNumber Selection::selectedLine() const
{
    return selectedLine_;
}

std::vector<LineNumber> Selection::getLines() const
{
    std::vector<LineNumber> selection;

    if ( selectedLine_.has_value() ) {
        selection.push_back( *selectedLine_ );
    }
    else if ( selectedPartial_.line.has_value() ) {
        selection.push_back( *selectedPartial_.line );
    }
    else if ( selectedRange_.startLine.has_value() ) {
        selection.resize( selectedRange_.size().get() );
        std::iota( selection.begin(), selection.end(), *selectedRange_.startLine );
    }

    return selection;
}

uint64_t Selection::getSelectedLinesCount() const { return selectedRange_.size().get(); }

// The tab behaviour is a bit odd at the moment, full lines are not expanded
// but partials (part of line) are, they probably should not ideally.
QString Selection::getSelectedText( const AbstractLogData* logData, bool lineNumbers ) const
{
    const auto selectionData = getSelectionWithLineNumbers( logData );

    QString text;

    const auto selectionSizeEstimate = std::accumulate(
        selectionData.begin(), selectionData.end(), static_cast<int>( selectionData.size() ),
        []( const auto& acc, const auto& next ) { return acc + next.second.size(); } );

    text.reserve( selectionSizeEstimate );

    for ( const auto& [ lineNumber, line ] : selectionData ) {
        if ( !text.isEmpty() ) {
#if defined( Q_OS_WIN )
            text.append( QChar::CarriageReturn );
#endif
            text.append( QChar::LineFeed );
        }

        if ( lineNumbers ) {
            text.append( QStringLiteral( "%1: %2" ).arg( lineNumber.get() ).arg( line ) );
        }
        else {
            text.append( line );
        }
    }

    return text;
}

std::map<LineNumber, QString>
Selection::getSelectionWithLineNumbers( const AbstractLogData* logData ) const
{
    std::map<LineNumber, QString> selectionData;

    if ( selectedLine_.has_value() ) {
        selectionData.emplace( logData->getLineNumber( selectedLine_.value() ),
                               logData->getLineString( *selectedLine_ ) );
    }
    else if ( selectedPartial_.line.has_value() ) {
        selectionData.emplace(
            logData->getLineNumber( selectedPartial_.line.value() ),
            logData->getExpandedLineString( *selectedPartial_.line )
                .mid( selectedPartial_.startColumn,
                      ( selectedPartial_.endColumn - selectedPartial_.startColumn ) + 1 ) );
    }
    else if ( selectedRange_.startLine.has_value() ) {
        const auto list = logData->getLines( *selectedRange_.startLine, selectedRange_.size() );
        LineNumber ln = *selectedRange_.startLine;

        for ( const auto& line : list ) {
            selectionData.emplace( logData->getLineNumber( ln ), line );
            ln += 1_lnum;
        }
    }

    return selectionData;
}

FilePosition Selection::getNextPosition() const
{
    LineNumber line;
    int column = 0;

    if ( selectedLine_.has_value() ) {
        line = *selectedLine_ + 1_lcount;
    }
    else if ( selectedRange_.startLine.has_value() ) {
        line = selectedRange_.endLine + 1_lcount;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = selectedPartial_.endColumn + 1;
    }

    return FilePosition( line, column );
}

FilePosition Selection::getPreviousPosition() const
{
    LineNumber line = 0_lnum;
    int column = 0;

    if ( selectedLine_.has_value() ) {
        line = *selectedLine_;
    }
    else if ( selectedRange_.startLine.has_value() ) {
        line = *selectedRange_.startLine;
    }
    else if ( selectedPartial_.line.has_value() ) {
        line = *selectedPartial_.line;
        column = qMax( selectedPartial_.startColumn - 1, 0 );
    }

    return FilePosition( line, column );
}
